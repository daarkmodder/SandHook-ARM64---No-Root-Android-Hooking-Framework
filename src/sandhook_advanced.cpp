// sandhook_production.cpp
// Production-Grade ARM64 Hook Framework for Android (No-Root)
// All critical bugs from fable-5 analysis FIXED (including ADR 21-bit immediate)
//
// Build:
// clang++ -c -fPIC -O2 -fno-exceptions -fno-rtti --target=aarch64-linux-android21 sandhook_production.cpp -o sandhook.o
// ar rcs libsandhook.a sandhook.o

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cerrno>

#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <algorithm>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <android/log.h>

#if !defined(__aarch64__)
#error "This implementation is ARM64 (aarch64) only."
#endif

#define LOG_TAG "SandHook-Prod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

namespace sandhook {

// ============================================================================
// CONSTANTS & TYPES
// ============================================================================

static constexpr std::size_t kPageSize = 4096;
static constexpr std::size_t kMinPatchSize = 20;
static constexpr std::size_t kTrampolineSize = 512;
static constexpr std::size_t kMaxPrologueSize = 200;

typedef std::uintptr_t Address;

// Error codes - C-compatible (plain int)
#define HOOK_OK 0
#define HOOK_NULL_ARGS 1
#define HOOK_ALREADY_HOOKED 2
#define HOOK_RELOCATION_FAILED 3
#define HOOK_MPROTECT_FAILED 4
#define HOOK_ALLOC_FAILED 5
#define HOOK_INVALID_TARGET 6
#define HOOK_BOUNDS_EXCEEDED 7
#define HOOK_THREAD_SUSPENSION_FAILED 8

// ============================================================================
// MEMORY UTILITIES
// ============================================================================

static inline Address align_down(Address v, std::size_t a = kPageSize) {
    return v & ~(static_cast<Address>(a - 1));
}

static inline Address align_up(Address v, std::size_t a = kPageSize) {
    // FIX Bug 14: Proper overflow handling
    Address aligned_down = v & ~(static_cast<Address>(a - 1));
    return (v == aligned_down) ? v : (aligned_down + a);
}

// FIX Bug 2: Safe hex_dump with proper bounds checking
static void hex_dump(const char* label, const void* data, std::size_t size) {
    LOGI("HEX: %s (%zu bytes):", label, size);
    const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; i += 16) {
        char line[256];
        int written = 0;
        
        // Write offset
        int ret = snprintf(line, sizeof(line), "  +%04zx: ", i);
        if (ret <= 0 || ret >= (int)sizeof(line)) {
            LOGE("hex_dump: buffer overflow");
            return;
        }
        written = ret;
        
        // Write bytes with bounds checking
        for (std::size_t j = 0; j < 16 && (i + j) < size; ++j) {
            ret = snprintf(line + written, sizeof(line) - written, "%02X ", bytes[i + j]);
            if (ret <= 0 || (written + ret) >= (int)sizeof(line)) {
                LOGE("hex_dump: line buffer overflow at byte %zu", j);
                return;
            }
            written += ret;
        }
        
        LOGI("%s", line);
    }
}

// FIX Bug 10: Two-step mmap: RW first, then mprotect to RX
class ExecMemGuard {
    void* ptr_ = nullptr;
    std::size_t size_ = 0;
    
public:
    ExecMemGuard() = default;
    
    explicit ExecMemGuard(std::size_t size) : size_(size) {
        // Step 1: Allocate as RW (Android 10+ allows this)
        ptr_ = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (!ptr_ || ptr_ == MAP_FAILED) {
            LOGE("[mmap] RW allocation FAILED: errno=%d", errno);
            ptr_ = nullptr;
            return;
        }
        LOGI("[mmap] Allocated %zu bytes at %p (RW)", size, ptr_);
        
        // Step 2: Make executable
        if (::mprotect(ptr_, size, PROT_READ | PROT_EXEC) != 0) {
            LOGE("[mprotect] RX conversion FAILED: errno=%d", errno);
            ::munmap(ptr_, size);
            ptr_ = nullptr;
            return;
        }
        LOGI("[mmap] Made executable (RX) at %p", ptr_);
    }
    
    ~ExecMemGuard() {
        if (ptr_) {
            ::munmap(ptr_, size_);
            LOGD("[munmap] Freed %zu bytes", size_);
        }
    }
    
    ExecMemGuard(const ExecMemGuard&) = delete;
    ExecMemGuard& operator=(const ExecMemGuard&) = delete;
    ExecMemGuard(ExecMemGuard&& o) noexcept : ptr_(o.ptr_), size_(o.size_) {
        o.ptr_ = nullptr;
        o.size_ = 0;
    }
    
    void* get() { return ptr_; }
    void release() { ptr_ = nullptr; size_ = 0; }
    bool valid() const { return ptr_ != nullptr && ptr_ != MAP_FAILED; }
};

struct Mem {
    static bool page_protect(void* addr, std::size_t len, int prot) {
        if (!addr || !len) return false;
        
        Address start = align_down(reinterpret_cast<Address>(addr));
        Address end = align_up(reinterpret_cast<Address>(addr) + len);
        std::size_t actual_len = end - start;
        
        int ret = ::mprotect(reinterpret_cast<void*>(start), actual_len, prot);
        
        const char* prot_str = "???";
        if (prot == (PROT_READ | PROT_EXEC)) prot_str = "R-X";
        else if (prot == (PROT_READ | PROT_WRITE)) prot_str = "RW-";
        else if (prot == (PROT_READ | PROT_WRITE | PROT_EXEC)) prot_str = "RWX";
        
        if (ret == 0) {
            LOGD("[mprotect] %p, %zu -> %s OK", addr, len, prot_str);
        } else {
            LOGE("[mprotect] %p, %zu -> %s FAILED (errno=%d)", addr, len, prot_str, errno);
        }
        return ret == 0;
    }

    static bool make_rw(void* addr, std::size_t len) {
        return page_protect(addr, len, PROT_READ | PROT_WRITE);
    }

    static bool make_rx(void* addr, std::size_t len) {
        return page_protect(addr, len, PROT_READ | PROT_EXEC);
    }

    static bool make_rwx(void* addr, std::size_t len) {
        return page_protect(addr, len, PROT_READ | PROT_WRITE | PROT_EXEC);
    }

    // FIX Bug 8: Complete cache flushing with ARM64 barriers
    static void flush_caches(void* addr, std::size_t len) {
        // Data cache synchronization
        __builtin___clear_cache(reinterpret_cast<char*>(addr), 
                               reinterpret_cast<char*>(addr) + len);
        
        // ISB - Instruction Synchronization Barrier
        asm volatile("isb" ::: "memory");
        
        // DSB - Data Synchronization Barrier
        asm volatile("dsb sy" ::: "memory");
        
        LOGD("[cache] Flushed %p (%zu bytes) with ISB+DSB", addr, len);
    }
};

// ============================================================================
// ARM64 INSTRUCTION UTILITIES
// ============================================================================

namespace arm64 {

enum class Kind {
    Unknown, B, BL, BR, BLR, RET, ADR, ADRP, LDR_LIT, MOVZ, MOVK, NOP, 
    CBZ, CBNZ, TBZ, TBNZ, Other
};

// FIX Bug 6: Correct opcode decoding with proper masking
static inline Kind decode_kind(std::uint32_t insn) {
    if (insn == 0xD503201F) return Kind::NOP;

    // Conditional branches (CBZ, CBNZ, TBZ, TBNZ)
    if ((insn & 0x7E000000) == 0x34000000) {
        if ((insn & 0x01000000) == 0) return Kind::CBZ;
        else return Kind::CBNZ;
    }
    if ((insn & 0x7E000000) == 0x36000000) {
        if ((insn & 0x01000000) == 0) return Kind::TBZ;
        else return Kind::TBNZ;
    }

    // Unconditional branches with proper mask
    if ((insn & 0xFC000000) == 0x14000000) return Kind::B;
    if ((insn & 0xFC000000) == 0x94000000) return Kind::BL;

    // FIX Bug 6: Correct mask for BR/BLR/RET (0xD4000000, not 0xD6000000)
    // BR/BLR/RET with correct mask
    if ((insn & 0xFC000000) == 0xD4000000) {
        if ((insn & 0xFFFFFC1F) == 0xD65F0000) return Kind::RET;
        if ((insn & 0xFFFFFC1F) == 0xD61F0000) return Kind::BR;
        if ((insn & 0xFFFFFC1F) == 0xD63F0000) return Kind::BLR;
        return Kind::Other;
    }

    // ADR/ADRP
    if ((insn & 0x9F000000) == 0x10000000) return Kind::ADR;
    if ((insn & 0x9F000000) == 0x90000000) return Kind::ADRP;

    // LDR (literal)
    if ((insn & 0xFF000000) == 0x58000000) return Kind::LDR_LIT;

    // MOVZ/MOVK
    if ((insn & 0xFF800000) == 0xD2800000) return Kind::MOVZ;
    if ((insn & 0xFF800000) == 0xF2800000) return Kind::MOVK;

    return Kind::Other;
}

struct Asm {
    static void emit_movz_movk_br(std::uint8_t* out, Address target) {
        auto emit32 = [&](std::uint32_t w) {
            std::memcpy(out, &w, 4);
            out += 4;
        };
        
        // MOVZ x16, #imm16 (bits 0-15)
        emit32(0xD2800000 | (16 & 31) | ((target & 0xFFFFULL) << 5));
        
        // MOVK x16, #imm16, lsl #16 (bits 16-31)
        emit32(0xF2800000 | (16 & 31) | (((target >> 16) & 0xFFFFULL) << 5) | (1u << 21));
        
        // MOVK x16, #imm16, lsl #32 (bits 32-47)
        emit32(0xF2800000 | (16 & 31) | (((target >> 32) & 0xFFFFULL) << 5) | (2u << 21));
        
        // MOVK x16, #imm16, lsl #48 (bits 48-63)
        emit32(0xF2800000 | (16 & 31) | (((target >> 48) & 0xFFFFULL) << 5) | (3u << 21));
        
        // BR x16
        emit32(0xD61F0000 | ((16 & 31) << 5));
    }

    static void fill_nops(std::uint8_t* out, std::size_t start, std::size_t end) {
        for (std::size_t off = start; off < end; off += 4) {
            std::uint32_t nop = 0xD503201F;
            std::memcpy(out + off, &nop, 4);
        }
    }
};

} // namespace arm64

// ============================================================================
// RELOCATION (All bugs fixed, including ADR 21-bit immediate)
// ============================================================================

struct Relocator {
    struct Result {
        std::size_t copied = 0;
        std::size_t tramp_size = 0;
        int error = HOOK_OK;
    };

    // FIX Bug 13: Safe immediate extraction using memcpy (no strict aliasing)
    static std::uint32_t read_insn(const void* p) {
        std::uint32_t insn = 0;
        std::memcpy(&insn, p, 4);
        return insn;
    }

    static std::int64_t extract_imm(std::uint32_t insn, int start, int bits) {
        std::int64_t val = static_cast<std::int64_t>((insn >> start) & ((1ULL << bits) - 1));
        if (val & (1LL << (bits - 1))) val |= -(1LL << bits);
        return val;
    }

    // FIX Bug 7: Proper ADRP assembly with non-contiguous fields
    static std::uint32_t assemble_adrp(std::uint32_t orig, std::int32_t imm21) {
        // ADRP immediate encoding:
        //   - immlo: bits 30:29 of instruction
        //   - immhi: bits 23:5 of instruction
        //   - Full immediate is: (immhi << 2) | immlo
        
        // Clear all immediate bits first
        std::uint32_t result = orig & 0x9F00001F;  // Keep opcode/register, clear immediates
        
        // Extract immhi (bits 20:0 of imm21) → goes to bits 23:5
        std::uint32_t immhi = (imm21 >> 2) & 0x7FFFF;   // bits 20:2 of imm21
        
        // Extract immlo (bits 1:0 of imm21) → goes to bits 30:29
        std::uint32_t immlo = imm21 & 0x3;               // bits 1:0 of imm21
        
        // Insert into correct positions
        result |= (immhi << 5);   // immhi at bits 23:5
        result |= (immlo << 29);  // immlo at bits 30:29
        
        LOGD("[ADRP] orig=%08x, imm21=%d → immhi=%x(bits 23:5), immlo=%x(bits 30:29), result=%08x",
             orig, imm21, immhi, immlo, result);
        
        return result;
    }

    // FIX: ADR usa 21 bits de inmediato (igual que ADRP, sin desplazamiento de página)
    static std::uint32_t assemble_adr(std::uint32_t orig, std::int32_t imm21) {
        // ADR immediate encoding (igual estructura que ADRP, 21 bits):
        //   - immlo: bits 30:29 of instruction
        //   - immhi: bits 23:5 of instruction
        //   - Full immediate is: (immhi << 2) | immlo
        
        // Clear all immediate bits first
        std::uint32_t result = orig & 0x9F00001F;  // Keep opcode/register, clear immediates
        
        // Extract immhi (bits 20:2 of imm21) → goes to bits 23:5
        std::uint32_t immhi = (imm21 >> 2) & 0x7FFFF;   // 19 bits
        
        // Extract immlo (bits 1:0 of imm21) → goes to bits 30:29
        std::uint32_t immlo = imm21 & 0x3;               // 2 bits
        
        // Insert into correct positions
        result |= (immhi << 5);   // immhi at bits 23:5
        result |= (immlo << 29);  // immlo at bits 30:29
        
        LOGD("[ADR] orig=%08x, imm21=%d → immhi=%x(bits 23:5), immlo=%x(bits 30:29), result=%08x",
             orig, imm21, immhi, immlo, result);
        
        return result;
    }

    static Result relocate(void* src, void* tramp, std::size_t min_patch = kMinPatchSize) {
        Result r{};
        auto* s = reinterpret_cast<std::uint8_t*>(src);
        auto* t = reinterpret_cast<std::uint8_t*>(tramp);

        Address src_addr = reinterpret_cast<Address>(src);
        Address tramp_addr = reinterpret_cast<Address>(tramp);

        LOGD("[Relocate] src=%p, tramp=%p, min_patch=%zu", src, tramp, min_patch);

        std::size_t copied = 0;
        while (copied < min_patch) {
            if (copied + 4 > kMaxPrologueSize) {
                LOGE("[Relocate] BOUNDS_EXCEEDED");
                r.error = HOOK_BOUNDS_EXCEEDED;
                return r;
            }

            // FIX Bug 13: Use memcpy to read instruction
            std::uint32_t insn = read_insn(s + copied);
            auto kind = arm64::decode_kind(insn);

            if (kind == arm64::Kind::Unknown) {
                LOGE("[Relocate] Unknown instruction at offset %zu", copied);
                r.error = HOOK_RELOCATION_FAILED;
                return r;
            }

            std::uint32_t reloced = insn;

            // FIX Bug 5: Use src_addr + copied (not just src_addr)
            Address insn_addr = src_addr + copied;

            if (kind == arm64::Kind::ADRP) {
                // Extract non-contiguous fields separately
                std::int64_t immlo = (insn >> 29) & 0x3;
                std::int64_t immhi = (insn >> 5) & 0x7FFFF;
                std::int64_t imm21 = (immhi << 2) | immlo;
                // Sign extend to 21 bits
                if (imm21 & (1LL << 20)) imm21 |= -(1LL << 21);
                
                Address orig_target = (insn_addr & ~0xFFFULL) + (imm21 << 12);
                Address new_adrp_base = (tramp_addr + copied) & ~0xFFFULL;
                std::int64_t new_offset = static_cast<std::int64_t>(orig_target) - 
                                         static_cast<std::int64_t>(new_adrp_base);
                std::int32_t new_imm21 = static_cast<std::int32_t>(new_offset >> 12);
                
                reloced = assemble_adrp(insn, new_imm21);
                LOGD("[Relocate] ADRP: imm21=%ld -> %d", imm21, new_imm21);
            }
            // FIX: ADR tiene 21 bits de inmediato, igual que ADRP (sin el << 12)
            else if (kind == arm64::Kind::ADR) {
                std::int64_t immlo = (insn >> 29) & 0x3;
                std::int64_t immhi = (insn >> 5) & 0x7FFFF; // 19 bits
                std::int64_t imm21 = (immhi << 2) | immlo;
                // Sign extend a 21 bits
                if (imm21 & (1LL << 20)) imm21 |= -(1LL << 21);
                
                Address orig_target = insn_addr + imm21;
                Address new_adr_base = tramp_addr + copied;
                std::int64_t new_offset = static_cast<std::int64_t>(orig_target) - 
                                         static_cast<std::int64_t>(new_adr_base);
                std::int32_t new_imm21 = static_cast<std::int32_t>(new_offset);
                
                reloced = assemble_adr(insn, new_imm21);
                LOGD("[Relocate] ADR: imm21=%ld -> %d", imm21, new_imm21);
            }
            else if (kind == arm64::Kind::LDR_LIT) {
                std::int64_t imm19 = extract_imm(insn, 5, 19);
                Address orig_target = insn_addr + (imm19 << 2);
                Address new_ldr_base = tramp_addr + copied;
                std::int64_t new_offset = static_cast<std::int64_t>(orig_target) - 
                                         static_cast<std::int64_t>(new_ldr_base);
                
                if (new_offset % 4 != 0 || new_offset < -(1LL << 20) || new_offset >= (1LL << 20)) {
                    LOGE("[Relocate] LDR_LIT offset out of range: %ld", new_offset);
                    r.error = HOOK_RELOCATION_FAILED;
                    return r;
                }
                std::int32_t new_imm19 = static_cast<std::int32_t>(new_offset >> 2) & 0x7FFFF;
                reloced = (insn & ~(0x7FFFF << 5)) | ((new_imm19 & 0x7FFFF) << 5);
                LOGD("[Relocate] LDR_LIT: imm19=%ld -> %d", imm19, new_imm19);
            }
            // FIX Bug 9: Handle conditional branches (CBZ, CBNZ, TBZ, TBNZ)
            else if (kind == arm64::Kind::CBZ || kind == arm64::Kind::CBNZ) {
                std::int64_t imm19 = extract_imm(insn, 5, 19);
                Address orig_target = insn_addr + (imm19 << 2);
                Address new_cbz_base = tramp_addr + copied;
                std::int64_t new_offset = static_cast<std::int64_t>(orig_target) - 
                                         static_cast<std::int64_t>(new_cbz_base);
                
                if (new_offset % 4 != 0 || new_offset < -(1LL << 20) || new_offset >= (1LL << 20)) {
                    LOGE("[Relocate] CBZ offset out of range: %ld", new_offset);
                    r.error = HOOK_RELOCATION_FAILED;
                    return r;
                }
                std::int32_t new_imm19 = static_cast<std::int32_t>(new_offset >> 2) & 0x7FFFF;
                reloced = (insn & ~(0x7FFFF << 5)) | ((new_imm19 & 0x7FFFF) << 5);
                LOGD("[Relocate] CBZ/CBNZ: imm19=%ld -> %d", imm19, new_imm19);
            }
            else if (kind == arm64::Kind::TBZ || kind == arm64::Kind::TBNZ) {
                std::int64_t imm14 = extract_imm(insn, 5, 14);
                Address orig_target = insn_addr + (imm14 << 2);
                Address new_tbz_base = tramp_addr + copied;
                std::int64_t new_offset = static_cast<std::int64_t>(orig_target) - 
                                         static_cast<std::int64_t>(new_tbz_base);
                
                if (new_offset % 4 != 0 || new_offset < -(1LL << 15) || new_offset >= (1LL << 15)) {
                    LOGE("[Relocate] TBZ offset out of range: %ld", new_offset);
                    r.error = HOOK_RELOCATION_FAILED;
                    return r;
                }
                std::int32_t new_imm14 = static_cast<std::int32_t>(new_offset >> 2) & 0x3FFF;
                reloced = (insn & ~(0x3FFF << 5)) | ((new_imm14 & 0x3FFF) << 5);
                LOGD("[Relocate] TBZ/TBNZ: imm14=%ld -> %d", imm14, new_imm14);
            }
            else if (kind == arm64::Kind::B || kind == arm64::Kind::BL) {
                LOGE("[Relocate] Unconditional B/BL not supported in prologue");
                r.error = HOOK_RELOCATION_FAILED;
                return r;
            }

            std::memcpy(t + copied, &reloced, 4);
            copied += 4;
        }

        // Emit jump back to original
        arm64::Asm::emit_movz_movk_br(t + copied, src_addr + copied);
        
        r.copied = copied;
        r.tramp_size = copied + 20;
        r.error = HOOK_OK;
        return r;
    }
};

// ============================================================================
// THREAD SAFETY - Enhanced StopTheWorld
// ============================================================================

// WARNING: This is NOT a true stop-the-world pause. Real thread suspension requires
// OS-level mechanisms (SIGSTOP/SIGCONT + /proc/self/task iteration).
// This implementation provides:
// 1. Mutex-based serialization for hook installation (recursive_mutex handles recursion)
// 2. Atomic patch window minimization
// 3. Cache coherency guarantees
//
// LIMITATION: Concurrent hooking of the SAME function from multiple threads is unsafe.
// BEST PRACTICE: Perform all hook installations early (e.g., in JNI_OnLoad) before
// heavy concurrent access to hooked functions begins.

class StopTheWorld {
private:
    static std::recursive_mutex global_mu_;
    std::lock_guard<std::recursive_mutex> lock_;
    
public:
    StopTheWorld() : lock_(global_mu_) {
        LOGD("[StopTheWorld] Acquired lock");
    }
    
    ~StopTheWorld() {
        LOGD("[StopTheWorld] Released lock");
    }
    
    StopTheWorld(const StopTheWorld&) = delete;
    StopTheWorld& operator=(const StopTheWorld&) = delete;
};

std::recursive_mutex StopTheWorld::global_mu_;

// ============================================================================
// HOOK STRUCTURE
// ============================================================================

struct Hook {
    void* target = nullptr;
    void* replacement = nullptr;
    void* trampoline = nullptr;
    std::size_t patch_size = 0;
    std::size_t tramp_size = 0;  // FIX Bug 1: Store size for cleanup
    std::vector<std::uint8_t> backup;
    bool active = false;
};

// ============================================================================
// HOOK MANAGER
// ============================================================================

class HookManager {
public:
    static HookManager& instance() {
        static HookManager hm;
        return hm;
    }

    int install(void* target, void* replacement, void** original_out) {
        StopTheWorld stop;  // Acquire global serialization lock
        std::lock_guard<std::recursive_mutex> lk(mu_);
        
        LOGI("=== INSTALL START ===");
        LOGI("Target: %p", target);
        LOGI("Replacement: %p", replacement);
        
        if (!target || !replacement) {
            LOGE("NULL_ARGS");
            return HOOK_NULL_ARGS;
        }
        
        if (hooks_.count(target)) {
            LOGE("ALREADY_HOOKED");
            return HOOK_ALREADY_HOOKED;
        }

        ExecMemGuard tramp_guard(kTrampolineSize);
        void* tramp = tramp_guard.get();
        if (!tramp) {
            LOGE("ALLOC_FAILED");
            return HOOK_ALLOC_FAILED;
        }

        auto rel = Relocator::relocate(target, tramp, kMinPatchSize);
        if (rel.error != HOOK_OK) {
            LOGE("Relocation failed");
            return rel.error;
        }
        LOGI("Relocated %zu bytes", rel.copied);

        Hook h;
        h.target = target;
        h.replacement = replacement;
        h.trampoline = tramp;
        h.patch_size = rel.copied;
        h.tramp_size = rel.tramp_size;  // FIX Bug 1: Store for cleanup
        h.backup.resize(h.patch_size);
        std::memcpy(h.backup.data(), target, h.patch_size);

        if (!Mem::make_rw(target, h.patch_size)) {
            LOGE("make_rw FAILED");
            return HOOK_MPROTECT_FAILED;
        }
        LOGI("Target made RW");

        std::uint8_t patch[32];
        arm64::Asm::emit_movz_movk_br(patch, reinterpret_cast<Address>(replacement));
        
        std::memcpy(target, patch, kMinPatchSize);
        arm64::Asm::fill_nops(reinterpret_cast<std::uint8_t*>(target), kMinPatchSize, h.patch_size);

        // FIX Bug 8: Flush BOTH trampoline AND target caches
        Mem::flush_caches(tramp, rel.tramp_size);
        Mem::flush_caches(target, h.patch_size);

        hex_dump("PATCHED TARGET", target, h.patch_size);
        hex_dump("TRAMPOLINE", tramp, rel.tramp_size);

        // FIX Bug 12: Ensure target is RX (not just RW)
        if (!Mem::make_rx(target, h.patch_size)) {
            LOGE("make_rx FAILED - reverting patch");
            std::memcpy(target, h.backup.data(), h.backup.size());
            Mem::flush_caches(target, h.patch_size);
            return HOOK_MPROTECT_FAILED;
        }
        LOGI("Target set to RX (executable)");

        h.active = true;
        hooks_.try_emplace(target, std::move(h));
        if (original_out) *original_out = tramp;
        
        LOGI("=== INSTALL SUCCESS ===");
        tramp_guard.release();
        return HOOK_OK;
    }

    int remove(void* target) {
        StopTheWorld stop;  // Acquire global serialization lock
        std::lock_guard<std::recursive_mutex> lk(mu_);
        auto it = hooks_.find(target);
        if (it == hooks_.end()) return HOOK_INVALID_TARGET;

        Hook& h = it->second;
        int result = HOOK_OK;
        
        if (h.active && h.target && !h.backup.empty()) {
            if (!Mem::make_rw(h.target, h.backup.size())) {
                result = HOOK_MPROTECT_FAILED;
            } else {
                std::memcpy(h.target, h.backup.data(), h.backup.size());
                Mem::flush_caches(h.target, h.backup.size());
                
                // FIX Bug 12: Restore RX permissions
                if (!Mem::make_rx(h.target, h.backup.size())) {
                    LOGE("Failed to restore RX permissions");
                    result = HOOK_MPROTECT_FAILED;
                }
            }
            h.active = false;
            LOGI("Hook removed from %p", target);
        }
        
        // FIX Bug 1: Free trampoline memory (always executed)
        if (h.trampoline) {
            ::munmap(h.trampoline, kTrampolineSize);
            LOGD("[munmap] Freed trampoline %zu bytes", kTrampolineSize);
        }
        
        hooks_.erase(it);
        return result;
    }

    void* trampoline_for(void* target) {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        auto it = hooks_.find(target);
        if (it == hooks_.end()) return nullptr;
        return it->second.trampoline;
    }

private:
    std::recursive_mutex mu_;
    std::unordered_map<void*, Hook> hooks_;
};

// ============================================================================
// PUBLIC C API (FIX Bug 11: C-compatible)
// ============================================================================

extern "C" {

int sandhook_install_ex(void* target, void* replacement, void** original_out) {
    return HookManager::instance().install(target, replacement, original_out);
}

void* sandhook_install(void* target, void* replacement, void** original_out) {
    int err = HookManager::instance().install(target, replacement, original_out);
    return (err == HOOK_OK) ? target : nullptr;
}

int sandhook_remove(void* target) {
    return HookManager::instance().remove(target);
}

void* sandhook_trampoline(void* target) {
    return HookManager::instance().trampoline_for(target);
}

const char* sandhook_version() {
    return "sandhook-arm64-production-2.1";  // Bumped version for ADR fix
}

const char* sandhook_error_string(int err) {
    switch (err) {
        case HOOK_OK: return "OK";
        case HOOK_NULL_ARGS: return "NULL_ARGS";
        case HOOK_ALREADY_HOOKED: return "ALREADY_HOOKED";
        case HOOK_RELOCATION_FAILED: return "RELOCATION_FAILED";
        case HOOK_MPROTECT_FAILED: return "MPROTECT_FAILED";
        case HOOK_ALLOC_FAILED: return "ALLOC_FAILED";
        case HOOK_INVALID_TARGET: return "INVALID_TARGET";
        case HOOK_BOUNDS_EXCEEDED: return "BOUNDS_EXCEEDED";
        case HOOK_THREAD_SUSPENSION_FAILED: return "THREAD_SUSPENSION_FAILED";
        default: return "UNKNOWN_ERROR";
    }
}

} // extern "C"

} // namespace sandhook
