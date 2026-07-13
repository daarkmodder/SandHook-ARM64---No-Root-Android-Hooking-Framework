// sandhook_final_rwx.cpp
// ARM64 Hook Framework for Android No-Root
// Direct patching with RWX execution support
//
// Build:
// clang++ -c -fPIC -O2 -fno-exceptions -fno-rtti --target=aarch64-linux-android21 sandhook_final_rwx.cpp -o sandhook.o
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
#include <atomic>

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

#define LOG_TAG "SandHook-Final"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

namespace sandhook {

// ============================================================================
// CONSTANTS
// ============================================================================

static constexpr std::size_t kPageSize = 4096;
static constexpr std::size_t kMinPatchSize = 20;
static constexpr std::size_t kTrampolineSize = 256;
static constexpr std::size_t kMaxPrologueSize = 200;

// ============================================================================
// ERROR CODES
// ============================================================================

enum class HookError : int {
    OK = 0,
    NULL_ARGS = 1,
    ALREADY_HOOKED = 2,
    RELOCATION_FAILED = 3,
    MPROTECT_FAILED = 4,
    ALLOC_FAILED = 5,
    INVALID_TARGET = 6,
    BOUNDS_EXCEEDED = 7,
};

// ============================================================================
// MEMORY UTILITIES
// ============================================================================

static inline std::uintptr_t align_down(std::uintptr_t v, std::size_t a = kPageSize) {
    return v & ~(static_cast<std::uintptr_t>(a - 1));
}

static inline std::uintptr_t align_up(std::uintptr_t v, std::size_t a = kPageSize) {
    return (v + a - 1) & ~(static_cast<std::uintptr_t>(a - 1));
}

// Hex dump for debugging
static void hex_dump(const char* label, const void* data, std::size_t size) {
    LOGI("HEX: %s (%zu bytes):", label, size);
    const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; i += 16) {
        char line[128];
        std::size_t written = 0;
        written += snprintf(line + written, sizeof(line) - written, "  +%04zx: ", i);
        for (std::size_t j = 0; j < 16 && (i + j) < size; ++j) {
            written += snprintf(line + written, sizeof(line) - written, "%02X ", bytes[i + j]);
        }
        LOGI("%s", line);
    }
}

// RAII guard for executable memory
class ExecMemGuard {
    void* ptr_ = nullptr;
    std::size_t size_ = 0;
public:
    ExecMemGuard() = default;
    explicit ExecMemGuard(std::size_t size) : size_(size) {
        ptr_ = ::mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr_ && ptr_ != MAP_FAILED) {
            LOGI("mmap(%zu) -> %p", size, ptr_);
        } else {
            LOGE("mmap FAILED: errno=%d", errno);
            ptr_ = nullptr;
        }
    }
    ~ExecMemGuard() {
        if (ptr_) {
            ::munmap(ptr_, size_);
            LOGD("munmap(%p, %zu)", ptr_, size_);
        }
    }
    
    ExecMemGuard(const ExecMemGuard&) = delete;
    ExecMemGuard& operator=(const ExecMemGuard&) = delete;
    ExecMemGuard(ExecMemGuard&& o) noexcept : ptr_(o.ptr_), size_(o.size_) {
        o.ptr_ = nullptr; o.size_ = 0;
    }
    ExecMemGuard& operator=(ExecMemGuard&& o) noexcept {
        if (this != &o) {
            if (ptr_) ::munmap(ptr_, size_);
            ptr_ = o.ptr_; size_ = o.size_;
            o.ptr_ = nullptr; o.size_ = 0;
        }
        return *this;
    }
    
    void* get() { return ptr_; }
    void release() { ptr_ = nullptr; size_ = 0; }
    bool valid() const { return ptr_ != nullptr && ptr_ != MAP_FAILED; }
};

struct Mem {
    static bool page_protect(void* addr, std::size_t len, int prot) {
        if (!addr || !len) return false;
        std::uintptr_t start = align_down(reinterpret_cast<std::uintptr_t>(addr));
        std::uintptr_t end   = align_up(reinterpret_cast<std::uintptr_t>(addr) + len);
        std::size_t actual_len = end - start;
        
        int ret = ::mprotect(reinterpret_cast<void*>(start), actual_len, prot);
        
        const char* prot_str = "???";
        if (prot == (PROT_READ | PROT_EXEC)) prot_str = "R-X";
        else if (prot == (PROT_READ | PROT_WRITE)) prot_str = "RW-";
        else if (prot == (PROT_READ | PROT_WRITE | PROT_EXEC)) prot_str = "RWX";
        
        if (ret == 0) {
            LOGI("mprotect(%p, %zu, %s) -> OK", addr, len, prot_str);
        } else {
            LOGE("mprotect(%p, %zu, %s) -> FAILED (errno=%d)", addr, len, prot_str, errno);
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

    static void flush_icache(void* addr, std::size_t len) {
        __builtin___clear_cache(reinterpret_cast<char*>(addr), reinterpret_cast<char*>(addr) + len);
        LOGI("ICache flushed for %p", addr);
    }
};

// ============================================================================
// ARM64 INSTRUCTION UTILITIES
// ============================================================================

namespace arm64 {

enum class Kind {
    Unknown, B, BL, BR, BLR, RET, ADR, ADRP, LDR_LIT, MOVZ, MOVK, NOP, Other
};

static inline Kind decode_kind(std::uint32_t insn) {
    if (insn == 0xD503201F) return Kind::NOP;

    std::uint32_t hi = insn & 0xFFC00000;
    switch (hi) {
        case 0x14000000: return Kind::B;
        case 0x94000000: return Kind::BL;
        case 0xD6000000: {
            if ((insn & 0xFFFFFC1F) == 0xD65F0000) return Kind::RET;
            if ((insn & 0xFFFFFC1F) == 0xD61F0000) return Kind::BR;
            if ((insn & 0xFFFFFC1F) == 0xD63F0000) return Kind::BLR;
            return Kind::Other;
        }
        case 0x10000000:
            if ((insn & 0x9F000000) == 0x10000000) return Kind::ADR;
            break;
        case 0x90000000:
            if ((insn & 0x9F000000) == 0x90000000) return Kind::ADRP;
            break;
    }

    if ((insn & 0xFF000000) == 0x58000000) return Kind::LDR_LIT;
    if ((insn & 0xFF800000) == 0xD2800000) return Kind::MOVZ;
    if ((insn & 0xFF800000) == 0xF2800000) return Kind::MOVK;
    
    return Kind::Other;
}

static inline bool is_pc_relative(Kind kind) {
    return kind == Kind::B || kind == Kind::BL || kind == Kind::ADR || 
           kind == Kind::ADRP || kind == Kind::LDR_LIT;
}

struct Decoded {
    std::uint32_t insn = 0;
    Kind kind = Kind::Unknown;
    std::size_t size = 4;
    bool pc_relative = false;
};

struct Decoder {
    static Decoded decode(const void* p) {
        Decoded d;
        d.insn = *reinterpret_cast<const std::uint32_t*>(p);
        d.kind = decode_kind(d.insn);
        d.pc_relative = is_pc_relative(d.kind);
        return d;
    }
};

struct Asm {
    static void emit_movz_movk_br(std::uint8_t* out, std::uintptr_t target) {
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
// RELOCATION
// ============================================================================

struct Relocator {
    struct Result {
        std::size_t copied = 0;
        std::size_t tramp_size = 0;
        HookError error = HookError::OK;
    };

    static std::int64_t extract_imm(std::uint32_t insn, int start, int bits) {
        std::int64_t val = static_cast<std::int64_t>((insn >> start) & ((1ULL << bits) - 1));
        if (val & (1LL << (bits - 1))) val |= -(1LL << bits);
        return val;
    }

    static Result relocate(void* src, void* tramp, std::size_t min_patch = kMinPatchSize) {
        Result r{};
        auto* s = reinterpret_cast<std::uint8_t*>(src);
        auto* t = reinterpret_cast<std::uint8_t*>(tramp);

        std::uintptr_t src_addr = reinterpret_cast<std::uintptr_t>(src);
        std::uintptr_t tramp_addr = reinterpret_cast<std::uintptr_t>(tramp);

        LOGD("Relocate: src=%p, tramp=%p", src, tramp);

        std::size_t copied = 0;
        while (copied < min_patch) {
            if (copied + 4 > kMaxPrologueSize) {
                LOGE("BOUNDS_EXCEEDED");
                r.error = HookError::BOUNDS_EXCEEDED;
                return r;
            }

            auto d = arm64::Decoder::decode(s + copied);
            if (d.kind == arm64::Kind::Unknown) {
                LOGE("Unknown instruction");
                r.error = HookError::RELOCATION_FAILED;
                return r;
            }

            std::uint32_t insn = d.insn;
            std::uint32_t reloced = insn;

            if (d.kind == arm64::Kind::ADRP) {
                std::int64_t imm21 = extract_imm(insn, 5, 21);
                std::uintptr_t orig_target = (src_addr & ~0xFFFULL) + (imm21 << 12);
                std::uintptr_t new_adrp_base = (tramp_addr + copied) & ~0xFFFULL;
                std::int64_t new_offset = static_cast<std::int64_t>(orig_target) - 
                                         static_cast<std::int64_t>(new_adrp_base);
                std::int32_t new_imm21 = static_cast<std::int32_t>(new_offset >> 12) & 0x1FFFFF;
                reloced = (insn & ~(0x1FFFFF << 5)) | ((new_imm21 & 0x1FFFFF) << 5);
            }
            else if (d.kind == arm64::Kind::ADR) {
                std::int64_t imm20 = extract_imm(insn, 5, 20);
                std::uintptr_t orig_target = src_addr + imm20;
                std::uintptr_t new_adr_base = tramp_addr + copied;
                std::int64_t new_offset = static_cast<std::int64_t>(orig_target) - 
                                         static_cast<std::int64_t>(new_adr_base);
                std::int32_t new_imm20 = static_cast<std::int32_t>(new_offset) & 0xFFFFF;
                reloced = (insn & ~(0xFFFFF << 5)) | ((new_imm20 & 0xFFFFF) << 5);
            }
            else if (d.kind == arm64::Kind::LDR_LIT) {
                std::int64_t imm19 = extract_imm(insn, 5, 19);
                std::uintptr_t orig_target = src_addr + (imm19 << 2);
                std::uintptr_t new_ldr_base = tramp_addr + copied;
                std::int64_t new_offset = static_cast<std::int64_t>(orig_target) - 
                                         static_cast<std::int64_t>(new_ldr_base);
                
                if (new_offset % 4 != 0 || new_offset < -(1LL << 20) || new_offset >= (1LL << 20)) {
                    LOGE("LDR_LIT offset out of range");
                    r.error = HookError::RELOCATION_FAILED;
                    return r;
                }
                std::int32_t new_imm19 = static_cast<std::int32_t>(new_offset >> 2) & 0x7FFFF;
                reloced = (insn & ~(0x7FFFF << 5)) | ((new_imm19 & 0x7FFFF) << 5);
            }
            else if (d.kind == arm64::Kind::B || d.kind == arm64::Kind::BL) {
                LOGE("Branch not supported");
                r.error = HookError::RELOCATION_FAILED;
                return r;
            }

            std::memcpy(t + copied, &reloced, 4);
            copied += 4;
        }

        // Emit jump back to original
        arm64::Asm::emit_movz_movk_br(t + copied, reinterpret_cast<std::uintptr_t>(s) + copied);
        
        r.copied = copied;
        r.tramp_size = copied + 20;
        r.error = HookError::OK;
        return r;
    }
};

// ============================================================================
// HOOK MANAGEMENT
// ============================================================================

struct Hook {
    void* target = nullptr;
    void* replacement = nullptr;
    void* trampoline = nullptr;
    std::size_t patch_size = 0;
    std::vector<std::uint8_t> backup;
    bool active = false;
};

class HookManager {
public:
    static HookManager& instance() {
        static HookManager hm;
        return hm;
    }

    HookError install(void* target, void* replacement, void** original_out = nullptr) {
        std::lock_guard<std::mutex> lk(mu_);
        
        LOGI("=== INSTALL START ===");
        LOGI("Target: %p", target);
        LOGI("Replacement: %p", replacement);
        
        if (!target || !replacement) {
            LOGE("NULL_ARGS");
            return HookError::NULL_ARGS;
        }
        
        if (hooks_.count(target)) {
            LOGE("ALREADY_HOOKED");
            return HookError::ALREADY_HOOKED;
        }

        // Allocate trampoline
        ExecMemGuard tramp_guard(kTrampolineSize);
        void* tramp = tramp_guard.get();
        if (!tramp) {
            LOGE("ALLOC_FAILED");
            return HookError::ALLOC_FAILED;
        }

        // Relocate prologue
        auto rel = Relocator::relocate(target, tramp, kMinPatchSize);
        if (rel.error != HookError::OK) {
            LOGE("Relocation failed");
            return rel.error;
        }
        LOGI("Relocated %zu bytes", rel.copied);

        Hook h;
        h.target = target;
        h.replacement = replacement;
        h.trampoline = tramp;
        h.patch_size = rel.copied;
        h.backup.resize(h.patch_size);
        std::memcpy(h.backup.data(), target, h.patch_size);

        // Make target writable
        if (!Mem::make_rw(target, h.patch_size)) {
            LOGE("make_rw FAILED");
            return HookError::MPROTECT_FAILED;
        }
        LOGI("Target made RW");

        // Emit patch
        std::uint8_t patch[32];
        arm64::Asm::emit_movz_movk_br(patch, reinterpret_cast<std::uintptr_t>(replacement));
        
        LOGI("Patch size: %zu bytes", kMinPatchSize);
        
        if (kMinPatchSize > h.patch_size) {
            LOGE("BOUNDS_EXCEEDED");
            return HookError::BOUNDS_EXCEEDED;
        }

        // Write patch
        std::memcpy(target, patch, kMinPatchSize);
        
        // Fill remaining with NOPs
        arm64::Asm::fill_nops(reinterpret_cast<std::uint8_t*>(target), kMinPatchSize, h.patch_size);

        // Flush cache
        Mem::flush_icache(target, h.patch_size);

        hex_dump("PATCHED TARGET", target, h.patch_size);
        hex_dump("TRAMPOLINE", tramp, rel.tramp_size);

        // ✓ Set to RWX to ensure CPU can execute
        if (!Mem::make_rwx(target, h.patch_size)) {
            LOGE("make_rwx FAILED, but continuing (may still work)");
        } else {
            LOGI("Target set to RWX (executable)");
        }

        h.active = true;
        hooks_.try_emplace(target, std::move(h));
        if (original_out) *original_out = tramp;
        
        LOGI("=== INSTALL SUCCESS ===");
        tramp_guard.release();
        return HookError::OK;
    }

    HookError remove(void* target) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = hooks_.find(target);
        if (it == hooks_.end()) return HookError::INVALID_TARGET;

        Hook& h = it->second;
        if (h.active && h.target && !h.backup.empty()) {
            if (!Mem::make_rw(h.target, h.backup.size())) {
                return HookError::MPROTECT_FAILED;
            }
            std::memcpy(h.target, h.backup.data(), h.backup.size());
            Mem::flush_icache(h.target, h.backup.size());
            h.active = false;
            LOGI("Hook removed from %p", target);
        }
        hooks_.erase(it);
        return HookError::OK;
    }

    void* trampoline_for(void* target) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = hooks_.find(target);
        if (it == hooks_.end()) return nullptr;
        return it->second.trampoline;
    }

private:
    std::mutex mu_;
    std::unordered_map<void*, Hook> hooks_;
};

// ============================================================================
// PUBLIC C API
// ============================================================================

extern "C" {

HookError sandhook_install_ex(void* target, void* replacement, void** original_out) {
    return HookManager::instance().install(target, replacement, original_out);
}

void* sandhook_install(void* target, void* replacement, void** original_out) {
    HookError err = HookManager::instance().install(target, replacement, original_out);
    return (err == HookError::OK) ? target : nullptr;
}

int sandhook_remove(void* target) {
    HookError err = HookManager::instance().remove(target);
    return (err == HookError::OK) ? 0 : static_cast<int>(err);
}

void* sandhook_trampoline(void* target) {
    return HookManager::instance().trampoline_for(target);
}

const char* sandhook_version() {
    return "sandhook-arm64-final-rwx-0.5";
}

const char* sandhook_error_string(HookError err) {
    switch (err) {
        case HookError::OK: return "OK";
        case HookError::NULL_ARGS: return "NULL_ARGS";
        case HookError::ALREADY_HOOKED: return "ALREADY_HOOKED";
        case HookError::RELOCATION_FAILED: return "RELOCATION_FAILED";
        case HookError::MPROTECT_FAILED: return "MPROTECT_FAILED";
        case HookError::ALLOC_FAILED: return "ALLOC_FAILED";
        case HookError::INVALID_TARGET: return "INVALID_TARGET";
        case HookError::BOUNDS_EXCEEDED: return "BOUNDS_EXCEEDED";
        default: return "UNKNOWN_ERROR";
    }
}

} // extern "C"

} // namespace sandhook
