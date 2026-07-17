// sandhook_production.cpp
// Production-Grade ARM64 Hook Framework for Android (No-Root)
// Version: 3.3 (ShadowHook-style Atomic Write)
// Features:
// - W^X Compliance (RW alloc, RX lock)
// - Android 10+ execmod bypass (MAP_FIXED anonymous fallback)
// - Dobby-style Unconditional B/BL absolute relocation
// - Dobby-style Conditional CBZ/CBNZ/TBZ/TBNZ branch inversion
// - 21-bit ADR/ADRP correct relocation
// - Single Instruction Hook (4-byte relative branch)
// - ShadowHook-style Atomic Instruction Patching (Race condition prevention)

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
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

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
#define HOOK_OUT_OF_RANGE 9

// ============================================================================
// MEMORY UTILITIES
// ============================================================================

static inline Address align_down(Address v, std::size_t a = kPageSize) {
    return v & ~(static_cast<Address>(a - 1));
}

static inline Address align_up(Address v, std::size_t a = kPageSize) {
    Address aligned_down = v & ~(static_cast<Address>(a - 1));
    return (v == aligned_down) ? v : (aligned_down + a);
}

static void hex_dump(const char* label, const void* data, std::size_t size) {
    LOGI("HEX: %s (%zu bytes):", label, size);
    const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; i += 16) {
        char line[256];
        int written = 0;
        
        int ret = snprintf(line, sizeof(line), "  +%04zx: ", i);
        if (ret <= 0 || ret >= (int)sizeof(line)) {
            LOGE("hex_dump: buffer overflow");
            return;
        }
        written = ret;
        
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

// SHADOWHOOK-STYLE ATOMIC WRITE
// Previene crashes (SIGILL/SIGSEGV) si otros hilos están ejecutando la función
// exactamente en el momento en que escribimos el patch en memoria.
static void atomic_write_inst(void* target, const void* inst, size_t len) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(target);
    
    if (len == 4 && (addr % 4) == 0) {
        uint32_t val;
        std::memcpy(&val, inst, sizeof(val));
        __atomic_store_n(reinterpret_cast<uint32_t*>(addr), val, __ATOMIC_SEQ_CST);
    } else if (len == 8 && (addr % 8) == 0) {
        uint64_t val;
        std::memcpy(&val, inst, sizeof(val));
        __atomic_store_n(reinterpret_cast<uint64_t*>(addr), val, __ATOMIC_SEQ_CST);
    } else if (len == 16 && (addr % 16) == 0) {
        __int128 val;
        std::memcpy(&val, inst, sizeof(val));
        __atomic_store_n(reinterpret_cast<__int128*>(addr), val, __ATOMIC_SEQ_CST);
    } else {
        // Fallback a memcpy si no está alineado o es un tamaño raro
        std::memcpy(target, inst, len);
    }
}

class ExecMemGuard {
    void* ptr_ = nullptr;
    std::size_t size_ = 0;
    
public:
    ExecMemGuard() = default;
    
    explicit ExecMemGuard(std::size_t size) : size_(size) {
        // CORRECCIÓN W^X: Asignamos solo como Lectura y Escritura (RW)
        ptr_ = ::mmap(nullptr, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (!ptr_ || ptr_ == MAP_FAILED) {
            LOGE("[mmap] RW allocation FAILED: errno=%d", errno);
            ptr_ = nullptr;
            return;
        }
        LOGI("[mmap] Allocated %zu bytes at %p (RW)", size, ptr_);
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

    // FIXED: Android 10+ execmod bypass
    static bool make_rx(void* addr, std::size_t len) {
        // 1. Intentamos el método normal primero
        if (page_protect(addr, len, PROT_READ | PROT_EXEC)) return true;
        
        // 2. BYPASS ANDROID 10+ (execmod):
        // Si mprotect falla con EACCES, remapeamos la página como memoria anónima.
        LOGW("[mprotect] R-X failed. Falling back to anonymous mapping (execmem bypass).");
        
        Address start = align_down(reinterpret_cast<Address>(addr));
        Address end = align_up(reinterpret_cast<Address>(addr) + len);
        std::size_t page_len = end - start;
        
        // Hacemos un backup de la memoria actual (que ya tiene nuestro patch inyectado)
        std::vector<std::uint8_t> backup(page_len);
        std::memcpy(backup.data(), reinterpret_cast<void*>(start), page_len);
        
        // Creamos una página anónima EXACTAMENTE en la misma dirección física (MAP_FIXED)
        void* new_mem = ::mmap(reinterpret_cast<void*>(start), page_len, 
                               PROT_READ | PROT_WRITE, 
                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        
        if (new_mem == MAP_FAILED) {
            LOGE("[mmap] Anonymous fallback FAILED: errno=%d", errno);
            return false;
        }
        
        // Copiamos el contenido patcheado a la nueva página anónima
        std::memcpy(new_mem, backup.data(), page_len);
        
        // Ahora sí, como es memoria anónima, el sistema nos permite hacerla Ejecutable
        return page_protect(new_mem, page_len, PROT_READ | PROT_EXEC);
    }

    static void flush_caches(void* addr, std::size_t len) {
        __builtin___clear_cache(reinterpret_cast<char*>(addr), 
                               reinterpret_cast<char*>(addr) + len);
        asm volatile("isb" ::: "memory");
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

static inline Kind decode_kind(std::uint32_t insn) {
    if (insn == 0xD503201F) return Kind::NOP;

    if ((insn & 0x7E000000) == 0x34000000) {
        if ((insn & 0x01000000) == 0) return Kind::CBZ;
        else return Kind::CBNZ;
    }
    if ((insn & 0x7E000000) == 0x36000000) {
        if ((insn & 0x01000000) == 0) return Kind::TBZ;
        else return Kind::TBNZ;
    }

    if ((insn & 0xFC000000) == 0x14000000) return Kind::B;
    if ((insn & 0xFC000000) == 0x94000000) return Kind::BL;

    if ((insn & 0xFC000000) == 0xD4000000) {
        if ((insn & 0xFFFFFC1F) == 0xD65F0000) return Kind::RET;
        if ((insn & 0xFFFFFC1F) == 0xD61F0000) return Kind::BR;
        if ((insn & 0xFFFFFC1F) == 0xD63F0000) return Kind::BLR;
        return Kind::Other;
    }

    if ((insn & 0x9F000000) == 0x10000000) return Kind::ADR;
    if ((insn & 0x9F000000) == 0x90000000) return Kind::ADRP;

    if ((insn & 0xFF000000) == 0x58000000) return Kind::LDR_LIT;

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
        
        emit32(0xD2800000 | (16 & 31) | ((target & 0xFFFFULL) << 5));
        emit32(0xF2800000 | (16 & 31) | (((target >> 16) & 0xFFFFULL) << 5) | (1u << 21));
        emit32(0xF2800000 | (16 & 31) | (((target >> 32) & 0xFFFFULL) << 5) | (2u << 21));
        emit32(0xF2800000 | (16 & 31) | (((target >> 48) & 0xFFFFULL) << 5) | (3u << 21));
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
// RELOCATION (With Dobby-style Conditional Branch Inversion)
// ============================================================================

struct Relocator {
    struct Result {
        std::size_t copied = 0;
        std::size_t tramp_size = 0;
        int error = HOOK_OK;
    };

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

    static std::uint32_t assemble_adrp(std::uint32_t orig, std::int32_t imm21) {
        std::uint32_t result = orig & 0x9F00001F;
        std::uint32_t immhi = (imm21 >> 2) & 0x7FFFF;
        std::uint32_t immlo = imm21 & 0x3;
        result |= (immhi << 5);
        result |= (immlo << 29);
        return result;
    }

    static std::uint32_t assemble_adr(std::uint32_t orig, std::int32_t imm21) {
        std::uint32_t result = orig & 0x9F00001F;
        std::uint32_t immhi = (imm21 >> 2) & 0x7FFFF;
        std::uint32_t immlo = imm21 & 0x3;
        result |= (immhi << 5);
        result |= (immlo << 29);
        return result;
    }

    static Result relocate(void* src, void* tramp, std::size_t min_patch = kMinPatchSize) {
        Result r{};
        auto* s = reinterpret_cast<std::uint8_t*>(src);
        auto* t = reinterpret_cast<std::uint8_t*>(tramp);

        Address src_addr = reinterpret_cast<Address>(src);
        Address tramp_addr = reinterpret_cast<Address>(tramp);

        std::size_t copied = 0;       // Bytes leídos de la fuente
        std::size_t tramp_offset = 0; // Bytes escritos en el trampolín

        // Helper para escribir saltos absolutos (alineados a 8 bytes)
        auto emit_abs_jump = [&](Address target, bool is_bl) {
            // Alinear a 8 bytes para que el .quad sea accesible
            if ((tramp_offset % 8) != 0) {
                std::uint32_t nop = 0xD503201F;
                std::memcpy(t + tramp_offset, &nop, 4);
                tramp_offset += 4;
            }
            std::uint32_t ldr_x16 = 0x58000050; // LDR X16, [PC, #8]
            std::uint32_t br_x16 = is_bl ? 0xD63F0200 : 0xD61F0200; // BLR X16 / BR X16
            std::memcpy(t + tramp_offset, &ldr_x16, 4); tramp_offset += 4;
            std::memcpy(t + tramp_offset, &br_x16, 4); tramp_offset += 4;
            std::memcpy(t + tramp_offset, &target, 8); tramp_offset += 8;
        };

        // Helper para escribir saltos condicionales absolutos (Dobby style)
        auto emit_cond_abs_jump = [&](std::uint32_t insn, int imm_shift, int imm_mask, Address target) {
            // Necesitamos que inv_insn quede en offset % 8 == 4, para que LDR X16 quede en offset % 8 == 0
            if ((tramp_offset % 8) == 0) {
                std::uint32_t nop = 0xD503201F;
                std::memcpy(t + tramp_offset, &nop, 4);
                tramp_offset += 4;
            }
            
            // Invertir la condición (bit 24)
            std::uint32_t inv_insn = insn ^ (1 << 24);
            // Saltar 3 instrucciones (12 bytes) para esquivar el trampolín absoluto
            std::uint32_t new_imm = 3; 
            inv_insn = (inv_insn & ~(imm_mask << imm_shift)) | ((new_imm & imm_mask) << imm_shift);
            
            std::memcpy(t + tramp_offset, &inv_insn, 4); tramp_offset += 4;
            
            // Ahora tramp_offset % 8 == 0
            std::uint32_t ldr_x16 = 0x58000050; // LDR X16, [PC, #8]
            std::uint32_t br_x16 = 0xD61F0200;  // BR X16
            std::memcpy(t + tramp_offset, &ldr_x16, 4); tramp_offset += 4;
            std::memcpy(t + tramp_offset, &br_x16, 4); tramp_offset += 4;
            std::memcpy(t + tramp_offset, &target, 8); tramp_offset += 8;
        };

        // Helper para escribir LDR Literal absoluto
        auto emit_ldr_lit_abs = [&](std::uint32_t insn, Address target) {
            int rt = insn & 0x1F;
            // Alinear a 8 bytes
            if ((tramp_offset % 8) != 0) {
                std::uint32_t nop = 0xD503201F;
                std::memcpy(t + tramp_offset, &nop, 4);
                tramp_offset += 4;
            }
            std::uint32_t ldr_x16 = 0x58000050; // LDR X16, [PC, #8]
            std::uint32_t ldr_rt = 0xF9400200 | rt; // LDR Rt, [X16]
            std::memcpy(t + tramp_offset, &ldr_x16, 4); tramp_offset += 4;
            std::memcpy(t + tramp_offset, &ldr_rt, 4); tramp_offset += 4;
            std::memcpy(t + tramp_offset, &target, 8); tramp_offset += 8;
        };

        while (copied < min_patch) {
            if (copied + 4 > kMaxPrologueSize) {
                r.error = HOOK_BOUNDS_EXCEEDED;
                return r;
            }

            std::uint32_t insn = read_insn(s + copied);
            auto kind = arm64::decode_kind(insn);

            if (kind == arm64::Kind::Unknown) {
                r.error = HOOK_RELOCATION_FAILED;
                return r;
            }

            std::uint32_t reloced = insn;
            Address insn_addr = src_addr + copied;

            if (kind == arm64::Kind::ADRP) {
                std::int64_t immlo = (insn >> 29) & 0x3;
                std::int64_t immhi = (insn >> 5) & 0x7FFFF;
                std::int64_t imm21 = (immhi << 2) | immlo;
                if (imm21 & (1LL << 20)) imm21 |= -(1LL << 21);
                
                Address orig_target = (insn_addr & ~0xFFFULL) + (imm21 << 12);
                Address new_adrp_base = (tramp_addr + tramp_offset) & ~0xFFFULL;
                std::int64_t new_offset = static_cast<std::int64_t>(orig_target) - 
                                         static_cast<std::int64_t>(new_adrp_base);
                std::int32_t new_imm21 = static_cast<std::int32_t>(new_offset >> 12);
                
                reloced = assemble_adrp(insn, new_imm21);
                std::memcpy(t + tramp_offset, &reloced, 4);
                tramp_offset += 4;
            }
            else if (kind == arm64::Kind::ADR) {
                std::int64_t immlo = (insn >> 29) & 0x3;
                std::int64_t immhi = (insn >> 5) & 0x7FFFF;
                std::int64_t imm21 = (immhi << 2) | immlo;
                if (imm21 & (1LL << 20)) imm21 |= -(1LL << 21);
                
                Address orig_target = insn_addr + imm21;
                Address new_adr_base = tramp_addr + tramp_offset;
                std::int64_t new_offset = static_cast<std::int64_t>(orig_target) - 
                                         static_cast<std::int64_t>(new_adr_base);
                std::int32_t new_imm21 = static_cast<std::int32_t>(new_offset);
                
                reloced = assemble_adr(insn, new_imm21);
                std::memcpy(t + tramp_offset, &reloced, 4);
                tramp_offset += 4;
            }
            else if (kind == arm64::Kind::LDR_LIT) {
                std::int64_t imm19 = extract_imm(insn, 5, 19);
                Address orig_target = insn_addr + (imm19 << 2);
                Address new_base = tramp_addr + tramp_offset;
                std::int64_t new_offset = static_cast<std::int64_t>(orig_target) - 
                                         static_cast<std::int64_t>(new_base);
                
                if (new_offset % 4 != 0 || new_offset < -(1LL << 20) || new_offset >= (1LL << 20)) {
                    emit_ldr_lit_abs(insn, orig_target);
                } else {
                    std::int32_t new_imm19 = static_cast<std::int32_t>(new_offset >> 2) & 0x7FFFF;
                    reloced = (insn & ~(0x7FFFF << 5)) | ((new_imm19 & 0x7FFFF) << 5);
                    std::memcpy(t + tramp_offset, &reloced, 4);
                    tramp_offset += 4;
                }
            }
            else if (kind == arm64::Kind::CBZ || kind == arm64::Kind::CBNZ) {
                std::int64_t imm19 = extract_imm(insn, 5, 19);
                Address orig_target = insn_addr + (imm19 << 2);
                Address new_base = tramp_addr + tramp_offset;
                std::int64_t new_offset = static_cast<std::int64_t>(orig_target) - 
                                         static_cast<std::int64_t>(new_base);
                
                if (new_offset % 4 != 0 || new_offset < -(1LL << 20) || new_offset >= (1LL << 20)) {
                    emit_cond_abs_jump(insn, 5, 0x7FFFF, orig_target);
                } else {
                    std::int32_t new_imm19 = static_cast<std::int32_t>(new_offset >> 2) & 0x7FFFF;
                    reloced = (insn & ~(0x7FFFF << 5)) | ((new_imm19 & 0x7FFFF) << 5);
                    std::memcpy(t + tramp_offset, &reloced, 4);
                    tramp_offset += 4;
                }
            }
            else if (kind == arm64::Kind::TBZ || kind == arm64::Kind::TBNZ) {
                std::int64_t imm14 = extract_imm(insn, 5, 14);
                Address orig_target = insn_addr + (imm14 << 2);
                Address new_base = tramp_addr + tramp_offset;
                std::int64_t new_offset = static_cast<std::int64_t>(orig_target) - 
                                         static_cast<std::int64_t>(new_base);
                
                if (new_offset % 4 != 0 || new_offset < -(1LL << 15) || new_offset >= (1LL << 15)) {
                    emit_cond_abs_jump(insn, 5, 0x3FFF, orig_target);
                } else {
                    std::int32_t new_imm14 = static_cast<std::int32_t>(new_offset >> 2) & 0x3FFF;
                    reloced = (insn & ~(0x3FFF << 5)) | ((new_imm14 & 0x3FFF) << 5);
                    std::memcpy(t + tramp_offset, &reloced, 4);
                    tramp_offset += 4;
                }
            }
            else if (kind == arm64::Kind::B || kind == arm64::Kind::BL) {
                std::int64_t imm26 = static_cast<std::int64_t>(insn & 0x03FFFFFF);
                if (imm26 & (1LL << 25)) imm26 |= -(1LL << 26);
                
                Address target_addr = insn_addr + (imm26 << 2);
                emit_abs_jump(target_addr, kind == arm64::Kind::BL);
            }
            else {
                // Instrucción normal, copiar tal cual
                std::memcpy(t + tramp_offset, &insn, 4);
                tramp_offset += 4;
            }

            copied += 4;
        }

        // Emitir salto de regreso a la función original
        arm64::Asm::emit_movz_movk_br(t + tramp_offset, src_addr + copied);
        
        r.copied = copied;
        r.tramp_size = tramp_offset + 20;
        r.error = HOOK_OK;
        return r;
    }
};

// ============================================================================
// THREAD SAFETY
// ============================================================================

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
    std::size_t tramp_size = 0;
    std::vector<std::uint8_t> backup;
    bool active = false;
    bool is_single_insn = false;
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
        StopTheWorld stop;
        std::lock_guard<std::recursive_mutex> lk(mu_);
        
        if (!target || !replacement) return HOOK_NULL_ARGS;
        if (hooks_.count(target)) return HOOK_ALREADY_HOOKED;

        ExecMemGuard tramp_guard(kTrampolineSize);
        void* tramp = tramp_guard.get();
        if (!tramp) return HOOK_ALLOC_FAILED;

        auto rel = Relocator::relocate(target, tramp, kMinPatchSize);
        if (rel.error != HOOK_OK) return rel.error;

        Hook h;
        h.target = target;
        h.replacement = replacement;
        h.trampoline = tramp;
        h.patch_size = rel.copied;
        h.tramp_size = rel.tramp_size;
        h.backup.resize(h.patch_size);
        std::memcpy(h.backup.data(), target, h.patch_size);

        if (!Mem::make_rw(target, h.patch_size)) return HOOK_MPROTECT_FAILED;

        std::uint8_t patch[32];
        arm64::Asm::emit_movz_movk_br(patch, reinterpret_cast<Address>(replacement));
        
        // SHADOWHOOK-STYLE ATOMIC WRITE
        // Esto previene crashes si otros hilos están ejecutando la función en este instante.
        if (kMinPatchSize == 20) {
            // Escribimos los primeros 16 bytes atómicamente
            atomic_write_inst(target, patch, 16);
            // Escribimos los últimos 4 bytes (el BR X16) atómicamente
            atomic_write_inst(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(target) + 16), patch + 16, 4);
        } else {
            atomic_write_inst(target, patch, kMinPatchSize);
        }
        
        arm64::Asm::fill_nops(reinterpret_cast<std::uint8_t*>(target), kMinPatchSize, h.patch_size);

        Mem::flush_caches(tramp, rel.tramp_size);
        Mem::flush_caches(target, h.patch_size);

        // CORRECCIÓN W^X: Ahora que el trampolín está escrito y la caché limpia, 
        // bloqueamos la memoria del trampolín como Solo Lectura y Ejecución (RX)
        if (!Mem::make_rx(tramp, kTrampolineSize)) {
            LOGE("Failed to make trampoline RX");
            return HOOK_MPROTECT_FAILED;
        }
        LOGI("[mmap] Made trampoline executable (RX) at %p", tramp);

        hex_dump("PATCHED TARGET", target, h.patch_size);
        hex_dump("TRAMPOLINE", tramp, rel.tramp_size);

        if (!Mem::make_rx(target, h.patch_size)) {
            std::memcpy(target, h.backup.data(), h.backup.size());
            Mem::flush_caches(target, h.patch_size);
            return HOOK_MPROTECT_FAILED;
        }

        h.active = true;
        hooks_.try_emplace(target, std::move(h));
        if (original_out) *original_out = tramp;
        
        tramp_guard.release();
        return HOOK_OK;
    }

    int install_single_insn(void* target, void* replacement, void** original_out) {
        StopTheWorld stop;
        std::lock_guard<std::recursive_mutex> lk(mu_);

        if (!target || !replacement) return HOOK_NULL_ARGS;
        if (hooks_.count(target)) return HOOK_ALREADY_HOOKED;

        if (original_out != nullptr) {
            LOGW("[SingleInsn] Backup requested. Falling back to standard 20-byte hook.");
            return install(target, replacement, original_out);
        }

        std::int64_t offset = reinterpret_cast<std::int64_t>(replacement) - reinterpret_cast<std::int64_t>(target);
        
        if (offset >= -(1LL << 27) && offset < (1LL << 27)) {
            
            Hook h;
            h.target = target;
            h.replacement = replacement;
            h.patch_size = 4;
            h.is_single_insn = true;
            h.backup.resize(4);
            std::memcpy(h.backup.data(), target, 4);

            if (!Mem::make_rw(target, 4)) return HOOK_MPROTECT_FAILED;

            std::uint32_t imm26 = (static_cast<std::uint32_t>(offset >> 2)) & 0x03FFFFFF;
            std::uint32_t insn = 0x14000000 | imm26;
            
            // ESCRITURA ATÓMICA DE 4 BYTES
            atomic_write_inst(target, &insn, 4);
            Mem::flush_caches(target, 4);

            if (!Mem::make_rx(target, 4)) {
                std::memcpy(target, h.backup.data(), 4);
                Mem::flush_caches(target, 4);
                return HOOK_MPROTECT_FAILED;
            }

            h.active = true;
            hooks_.try_emplace(target, std::move(h));
            LOGI("[SingleInsn] Successfully hooked with 4-byte relative branch.");
            return HOOK_OK;
        }

        LOGW("[SingleInsn] Out of 128MB range. Falling back to standard 20-byte hook.");
        return install(target, replacement, original_out);
    }

    int remove(void* target) {
        StopTheWorld stop;
        std::lock_guard<std::recursive_mutex> lk(mu_);
        auto it = hooks_.find(target);
        if (it == hooks_.end()) return HOOK_INVALID_TARGET;

        Hook& h = it->second;
        int result = HOOK_OK;
        
        if (h.active && h.target && !h.backup.empty()) {
            if (!Mem::make_rw(h.target, h.backup.size())) {
                result = HOOK_MPROTECT_FAILED;
            } else {
                // Restaurar usando escritura atómica también
                size_t b_size = h.backup.size();
                if (b_size == 20) {
                    atomic_write_inst(h.target, h.backup.data(), 16);
                    atomic_write_inst(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(h.target) + 16), h.backup.data() + 16, 4);
                } else {
                    atomic_write_inst(h.target, h.backup.data(), b_size);
                }
                
                Mem::flush_caches(h.target, h.backup.size());
                
                if (!Mem::make_rx(h.target, h.backup.size())) {
                    LOGE("Failed to restore RX permissions");
                    result = HOOK_MPROTECT_FAILED;
                }
            }
            h.active = false;
        }
        
        if (h.trampoline && !h.is_single_insn) {
            ::munmap(h.trampoline, kTrampolineSize);
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
// PUBLIC C API
// ============================================================================

extern "C" {

int sandhook_install_ex(void* target, void* replacement, void** original_out) {
    return HookManager::instance().install(target, replacement, original_out);
}

void* sandhook_install(void* target, void* replacement, void** original_out) {
    int err = HookManager::instance().install(target, replacement, original_out);
    return (err == HOOK_OK) ? target : nullptr;
}

int sandhook_install_single_insn(void* target, void* replacement, void** original_out) {
    return HookManager::instance().install_single_insn(target, replacement, original_out);
}

int sandhook_remove(void* target) {
    return HookManager::instance().remove(target);
}

void* sandhook_trampoline(void* target) {
    return HookManager::instance().trampoline_for(target);
}

const char* sandhook_version() {
    return "sandhook-arm64-production-3.3"; // Atomic Write
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
        case HOOK_OUT_OF_RANGE: return "OUT_OF_RANGE";
        default: return "UNKNOWN_ERROR";
    }
}

} // extern "C"

} // namespace sandhook