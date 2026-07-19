// sandhook_production.cpp
// Production-Grade ARM64 Hook Framework for Android (No-Root)
// Version: 4.8 (Exec Mem Check + Dual Syscall Write Bypass)

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <dlfcn.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/uio.h>

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
#include <link.h>
#include <elf.h>
#include <android/log.h>
#include "xdl/xdl.h"

#ifndef PROT_BTI
#define PROT_BTI 0x10
#endif

#if defined(__ANDROID_API__) && __ANDROID_API__ >= 21
#include <android/dlext.h>
#endif

#if !defined(__aarch64__)
#error "This implementation is ARM64 (aarch64) only."
#endif

#define LOG_TAG "SandHook-Prod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// ============================================================================
// CFI BYPASS
// ============================================================================

#if defined(__aarch64__)
#define CFI_ARM64_RET_INST 0xd65f03c0

static bool g_cfi_disabled = false;
static uint32_t g_cfi_slowpath_backup = 0;
static uint32_t g_cfi_slowpath_diag_backup = 0;
static std::mutex g_cfi_mu;
static std::atomic<int> g_active_hook_count{0};

static void disableCFISlowpath() {
    std::lock_guard<std::mutex> lk(g_cfi_mu);
    if (g_cfi_disabled) return;

    void* slowpath = nullptr;
    void* slowpath_diag = nullptr;

    const char* lib_names[] = {"libc.so", "libdl.so", "linker64", nullptr};
    for (int i = 0; lib_names[i] != nullptr; ++i) {
        void* handle = xdl_open(lib_names[i], XDL_DEFAULT);
        if (!handle) continue;

        slowpath = xdl_sym(handle, "__cfi_slowpath", nullptr);
        if (!slowpath) slowpath = xdl_dsym(handle, "__cfi_slowpath", nullptr);

        slowpath_diag = xdl_sym(handle, "__cfi_slowpath_diag", nullptr);
        if (!slowpath_diag) slowpath_diag = xdl_dsym(handle, "__cfi_slowpath_diag", nullptr);

        xdl_close(handle);
        if (slowpath && slowpath_diag) break;
    }

    if (slowpath && slowpath_diag) {
        uintptr_t start = (uintptr_t)slowpath < (uintptr_t)slowpath_diag ? (uintptr_t)slowpath : (uintptr_t)slowpath_diag;
        uintptr_t end   = (uintptr_t)slowpath < (uintptr_t)slowpath_diag ? (uintptr_t)slowpath_diag : (uintptr_t)slowpath;
        size_t size = (end - start) + sizeof(uint32_t);

        uintptr_t page_start = start & ~0xFFFULL;
        size_t page_len = ((start + size - page_start) + 0xFFF) & ~0xFFFULL;

        if (syscall(SYS_mprotect, (void*)page_start, page_len, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            g_cfi_slowpath_backup = *((uint32_t*)slowpath);
            g_cfi_slowpath_diag_backup = *((uint32_t*)slowpath_diag);
            *((uint32_t*)slowpath) = CFI_ARM64_RET_INST;
            *((uint32_t*)slowpath_diag) = CFI_ARM64_RET_INST;
            __builtin___clear_cache((char*)start, (char*)(start + size));
            g_cfi_disabled = true;
            LOGI("[CFI] Slowpath disabled. Backups saved.");
        } else {
            LOGE("[CFI] mprotect failed: %d", errno);
        }
    } else {
        LOGE("[CFI] Could not find __cfi_slowpath via xDL.");
    }
}

static void restoreCFISlowpath() {
    std::lock_guard<std::mutex> lk(g_cfi_mu);
    if (!g_cfi_disabled) return;

    void* slowpath = nullptr;
    void* slowpath_diag = nullptr;

    const char* lib_names[] = {"libc.so", "libdl.so", "linker64", nullptr};
    for (int i = 0; lib_names[i] != nullptr; ++i) {
        void* handle = xdl_open(lib_names[i], XDL_DEFAULT);
        if (!handle) continue;

        slowpath = xdl_sym(handle, "__cfi_slowpath", nullptr);
        if (!slowpath) slowpath = xdl_dsym(handle, "__cfi_slowpath", nullptr);

        slowpath_diag = xdl_sym(handle, "__cfi_slowpath_diag", nullptr);
        if (!slowpath_diag) slowpath_diag = xdl_dsym(handle, "__cfi_slowpath_diag", nullptr);

        xdl_close(handle);
        if (slowpath && slowpath_diag) break;
    }

    if (slowpath && slowpath_diag) {
        uintptr_t start = (uintptr_t)slowpath < (uintptr_t)slowpath_diag ? (uintptr_t)slowpath : (uintptr_t)slowpath_diag;
        uintptr_t end   = (uintptr_t)slowpath < (uintptr_t)slowpath_diag ? (uintptr_t)slowpath_diag : (uintptr_t)slowpath;
        size_t size = (end - start) + sizeof(uint32_t);

        uintptr_t page_start = start & ~0xFFFULL;
        size_t page_len = ((start + size - page_start) + 0xFFF) & ~0xFFFULL;

        if (syscall(SYS_mprotect, (void*)page_start, page_len, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            *((uint32_t*)slowpath) = g_cfi_slowpath_backup;
            *((uint32_t*)slowpath_diag) = g_cfi_slowpath_diag_backup;
            __builtin___clear_cache((char*)start, (char*)(start + size));
            g_cfi_disabled = false;
            LOGI("[CFI] Slowpath restored from backup.");
        } else {
            LOGE("[CFI] Restore mprotect failed: %d", errno);
        }
    }
}

static void maybe_disable_cfi() {
    if (g_active_hook_count.fetch_add(1) == 0) {
        disableCFISlowpath();
    }
}

static void maybe_restore_cfi() {
    if (g_active_hook_count.fetch_sub(1) == 1) {
        restoreCFISlowpath();
    }
}
#else
static void disableCFISlowpath() {}
static void restoreCFISlowpath() {}
static void maybe_disable_cfi() {}
static void maybe_restore_cfi() {}
#endif

// ============================================================================
// GLOBAL SIGSEGV PROTECTION
// ============================================================================

struct SigHandlerState {
    std::atomic<bool> active{false};
    std::atomic<uintptr_t> range_start{0};
    std::atomic<uintptr_t> range_end{0};
    std::mutex mu;
    std::unordered_map<pid_t, sigjmp_buf*> thread_jmpbufs;
    std::atomic<int> handler_installed{0};
};

static SigHandlerState g_sig_state;
static struct sigaction g_old_sigact;

static void sandhook_sig_handler(int sig, siginfo_t* info, void* context) {
    (void)sig; (void)context;

    if (g_sig_state.active.load(std::memory_order_acquire)) {
        uintptr_t fault_addr = (uintptr_t)info->si_addr;
        uintptr_t r_start = g_sig_state.range_start.load(std::memory_order_relaxed);
        uintptr_t r_end   = g_sig_state.range_end.load(std::memory_order_relaxed);

        if (fault_addr >= r_start && fault_addr < r_end) {
            pid_t tid = gettid();
            std::lock_guard<std::mutex> lk(g_sig_state.mu);
            auto it = g_sig_state.thread_jmpbufs.find(tid);
            if (it != g_sig_state.thread_jmpbufs.end() && it->second) {
                siglongjmp(*it->second, 1);
            }
        }
    }

    if (g_old_sigact.sa_flags & SA_SIGINFO) {
        g_old_sigact.sa_sigaction(sig, info, context);
    } else if (g_old_sigact.sa_handler != SIG_IGN && g_old_sigact.sa_handler != SIG_DFL) {
        g_old_sigact.sa_handler(sig);
    } else {
        sigaction(SIGSEGV, &g_old_sigact, nullptr);
        raise(SIGSEGV);
    }
}

static void ensure_sig_handler_installed() {
    int expected = 0;
    if (g_sig_state.handler_installed.compare_exchange_strong(expected, 1)) {
        struct sigaction act;
        memset(&act, 0, sizeof(act));
        act.sa_sigaction = sandhook_sig_handler;
        act.sa_flags = SA_SIGINFO;
        sigemptyset(&act.sa_mask);
        sigaction(SIGSEGV, &act, &g_old_sigact);
    }
}

class SigGuard {
    sigjmp_buf jmpbuf_;
    bool jumped_ = false;
public:
    SigGuard(uintptr_t range_start, uintptr_t range_end) {
        ensure_sig_handler_installed();
        if (sigsetjmp(jmpbuf_, 1) == 0) {
            pid_t tid = gettid();
            {
                std::lock_guard<std::mutex> lk(g_sig_state.mu);
                g_sig_state.thread_jmpbufs[tid] = &jmpbuf_;
            }
            g_sig_state.range_start.store(range_start, std::memory_order_relaxed);
            g_sig_state.range_end.store(range_end, std::memory_order_relaxed);
            g_sig_state.active.store(true, std::memory_order_release);
        } else {
            jumped_ = true;
            pid_t tid = gettid();
            std::lock_guard<std::mutex> lk(g_sig_state.mu);
            g_sig_state.thread_jmpbufs.erase(tid);
            g_sig_state.active.store(false, std::memory_order_release);
        }
    }
    ~SigGuard() {
        if (!jumped_) {
            pid_t tid = gettid();
            std::lock_guard<std::mutex> lk(g_sig_state.mu);
            g_sig_state.thread_jmpbufs.erase(tid);
            g_sig_state.active.store(false, std::memory_order_release);
        }
    }
    bool jumped() const { return jumped_; }
};

// ============================================================================
// MEMORY VALIDATION
// ============================================================================

static bool is_region_strictly_rx(void* addr, size_t len) {
    if (!addr || len == 0) return false;
    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + len;

    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return false;

    char line[512];
    bool valid = true;
    while (fgets(line, sizeof(line), fp)) {
        uintptr_t seg_start = 0, seg_end = 0;
        char perms[5] = {0};
        if (sscanf(line, "%lx-%lx %4s", &seg_start, &seg_end, perms) != 3) continue;

        if (seg_end <= start || seg_start >= end) continue;

        if (perms[0] != 'r' || perms[1] != '-' || perms[2] != 'x') {
            valid = false;
            break;
        }
    }
    fclose(fp);
    return valid;
}

static bool is_system_critical_lib(void* target) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return false;
    char line[512];
    uintptr_t addr = (uintptr_t)target;
    bool is_critical = false;
    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start, end;
        if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
            if (addr >= start && addr < end) {
                if (strstr(line, "libdl.so") || strstr(line, "linker64")) {
                    is_critical = true;
                }
                break;
            }
        }
    }
    fclose(fp);
    return is_critical;
}

static bool is_executable_region(void* target) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return false;
    char line[512];
    uintptr_t addr = (uintptr_t)target;
    bool is_exec = false;
    while (fgets(line, sizeof(line), fp)) {
        uintptr_t start, end;
        if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
            if (addr >= start && addr < end) {
                if (strstr(line, " r-x ") || strstr(line, " r-xp ") || strstr(line, " r-xp")) {
                    is_exec = true;
                }
                break;
            }
        }
    }
    fclose(fp);
    return is_exec;
}

// ============================================================================
// SELinux & Anti-Tamper BYPASS: Dual Syscall Method
// ============================================================================

static bool write_mem_proc(void* addr, const void* data, size_t len) {
    // Strategy A: /proc/self/mem
    int fd = syscall(SYS_openat, AT_FDCWD, "/proc/self/mem", O_WRONLY | O_CLOEXEC, 0);
    if (fd >= 0) {
        if (syscall(SYS_lseek, fd, (off_t)addr, SEEK_SET) == (off_t)addr) {
            if (syscall(SYS_write, fd, data, len) == (ssize_t)len) {
                syscall(SYS_close, fd);
                return true;
            }
        }
        syscall(SYS_close, fd);
    }
    
    // Strategy B: process_vm_writev
    struct iovec local_iov;
    local_iov.iov_base = (void*)data;
    local_iov.iov_len = len;
    struct iovec remote_iov;
    remote_iov.iov_base = addr;
    remote_iov.iov_len = len;
    if (syscall(SYS_process_vm_writev, getpid(), &local_iov, 1, &remote_iov, 1, 0) == (ssize_t)len) {
        return true;
    }
    
    return false;
}

// ============================================================================
// MAIN FRAMEWORK
// ============================================================================

namespace sandhook {

static constexpr std::size_t kPageSize = 4096;
static constexpr std::size_t kMinPatchSize = 20;
static constexpr std::size_t kTrampolineSize = 512;
static constexpr std::size_t kMaxPrologueSize = 200;

typedef std::uintptr_t Address;

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

struct GOTHookEntry {
    void** slot;
    void* original;
};

struct PendingHook {
    std::string lib_name;
    std::string sym_name;
    void* replacement;
    void** original_out;
};

static std::vector<PendingHook> g_pending_hooks;
static std::mutex g_pending_mu;
static std::atomic<bool> g_dlopen_hooked{false};
static void* orig_dlopen_ptr = nullptr;
#if defined(__ANDROID_API__) && __ANDROID_API__ >= 21
static void* orig_android_dlopen_ext_ptr = nullptr;
#endif
static std::vector<GOTHookEntry> g_dlopen_got_entries;

static void apply_pending_hooks_for_lib(const char* loaded_lib_name);
static void init_dlopen_monitor();
static bool got_hook_all_modules(void* target, void* replacement, std::vector<GOTHookEntry>& entries);

static inline Address align_down(Address v, std::size_t a = kPageSize) {
    return v & ~(static_cast<Address>(a - 1));
}

static inline Address align_up(Address v, std::size_t a = kPageSize) {
    Address aligned_down = v & ~(static_cast<Address>(a - 1));
    return (v == aligned_down) ? v : (aligned_down + a);
}

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
        std::memcpy(target, inst, len);
    }
}

class ExecMemGuard {
    void* ptr_ = nullptr;
    std::size_t size_ = 0;
public:
    ExecMemGuard() = default;
    explicit ExecMemGuard(std::size_t size) : size_(size) {
        ptr_ = (void*)syscall(SYS_mmap, nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (!ptr_ || ptr_ == MAP_FAILED) { ptr_ = nullptr; }
    }
    ~ExecMemGuard() { if (ptr_) syscall(SYS_munmap, ptr_, size_); }
    ExecMemGuard(const ExecMemGuard&) = delete;
    ExecMemGuard& operator=(const ExecMemGuard&) = delete;
    ExecMemGuard(ExecMemGuard&& o) noexcept : ptr_(o.ptr_), size_(o.size_) { o.ptr_ = nullptr; o.size_ = 0; }
    void* get() { return ptr_; }
    void release() { ptr_ = nullptr; size_ = 0; }
};

struct Mem {
    static bool page_protect(void* addr, std::size_t len, int prot) {
        if (!addr || !len) return false;
        Address start = align_down(reinterpret_cast<Address>(addr));
        Address end = align_up(reinterpret_cast<Address>(addr) + len);
        std::size_t actual_len = end - start;
        return syscall(SYS_mprotect, reinterpret_cast<void*>(start), actual_len, prot) == 0;
    }

    static bool make_rw(void* addr, std::size_t len) { return page_protect(addr, len, PROT_READ | PROT_WRITE); }

    static bool make_rx(void* addr, std::size_t len) {
        if (page_protect(addr, len, PROT_READ | PROT_EXEC)) return true;
        if (page_protect(addr, len, PROT_READ | PROT_EXEC | PROT_BTI)) return true;

        if (!is_region_strictly_rx(addr, len)) {
            return false;
        }

        Address start = align_down(reinterpret_cast<Address>(addr));
        Address end = align_up(reinterpret_cast<Address>(addr) + len);
        std::size_t page_len = end - start;
        std::vector<std::uint8_t> backup(page_len);
        std::memcpy(backup.data(), reinterpret_cast<void*>(start), page_len);
        
        void* new_mem = (void*)syscall(SYS_mmap, reinterpret_cast<void*>(start), page_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
        if (new_mem == MAP_FAILED) return false;
        std::memcpy(new_mem, backup.data(), page_len);
        
        if (page_protect(new_mem, page_len, PROT_READ | PROT_EXEC)) return true;
        return page_protect(new_mem, page_len, PROT_READ | PROT_EXEC | PROT_BTI);
    }

    static void flush_caches(void* addr, std::size_t len) {
        __builtin___clear_cache(reinterpret_cast<char*>(addr), reinterpret_cast<char*>(addr) + len);
        asm volatile("isb" ::: "memory");
        asm volatile("dsb sy" ::: "memory");
    }
};

namespace arm64 {

enum class Kind { Unknown, B, BL, BR, BLR, RET, ADR, ADRP, LDR_LIT, MOVZ, MOVK, NOP, CBZ, CBNZ, TBZ, TBNZ, Other };

static inline Kind decode_kind(std::uint32_t insn) {
    if (insn == 0xD503201F) return Kind::NOP;
    if ((insn & 0x7E000000) == 0x34000000) return ((insn & 0x01000000) == 0) ? Kind::CBZ : Kind::CBNZ;
    if ((insn & 0x7E000000) == 0x36000000) return ((insn & 0x01000000) == 0) ? Kind::TBZ : Kind::TBNZ;
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
        auto emit32 = [&](std::uint32_t w) { std::memcpy(out, &w, 4); out += 4; };
        emit32(0xD2800000 | (16 & 31) | ((target & 0xFFFFULL) << 5));
        emit32(0xF2800000 | (16 & 31) | (((target >> 16) & 0xFFFFULL) << 5) | (1u << 21));
        emit32(0xF2800000 | (16 & 31) | (((target >> 32) & 0xFFFFULL) << 5) | (2u << 21));
        emit32(0xF2800000 | (16 & 31) | (((target >> 48) & 0xFFFFULL) << 5) | (3u << 21));
        emit32(0xD61F0000 | ((16 & 31) << 5));
    }
    static void fill_nops(std::uint8_t* out, std::size_t start, std::size_t end) {
        for (std::size_t off = start; off < end; off += 4) {
            std::uint32_t nop = 0xD503201F; std::memcpy(out + off, &nop, 4);
        }
    }
};
} // namespace arm64

struct Relocator {
    struct Result { std::size_t copied = 0; std::size_t tramp_size = 0; int error = HOOK_OK; };

    static std::uint32_t read_insn(const void* p) { std::uint32_t insn = 0; std::memcpy(&insn, p, 4); return insn; }
    static std::int64_t extract_imm(std::uint32_t insn, int start, int bits) {
        std::int64_t val = static_cast<std::int64_t>((insn >> start) & ((1ULL << bits) - 1));
        if (val & (1LL << (bits - 1))) val |= -(1LL << bits);
        return val;
    }
    static std::uint32_t assemble_adrp(std::uint32_t orig, std::int32_t imm21) {
        std::uint32_t result = orig & 0x9F00001F;
        result |= (((imm21 >> 2) & 0x7FFFF) << 5); result |= ((imm21 & 0x3) << 29); return result;
    }
    static std::uint32_t assemble_adr(std::uint32_t orig, std::int32_t imm21) {
        std::uint32_t result = orig & 0x9F00001F;
        result |= (((imm21 >> 2) & 0x7FFFF) << 5); result |= ((imm21 & 0x3) << 29); return result;
    }

    static Result relocate(void* src, void* tramp, std::size_t min_patch = kMinPatchSize) {
        Result r{};
        auto* s = reinterpret_cast<std::uint8_t*>(src);
        auto* t = reinterpret_cast<std::uint8_t*>(tramp);
        Address src_addr = reinterpret_cast<Address>(src);
        Address tramp_addr = reinterpret_cast<Address>(tramp);
        std::size_t copied = 0, tramp_offset = 0;

        auto emit_abs_jump = [&](Address target, bool is_bl) {
            if ((tramp_offset % 8) != 0) { std::uint32_t nop = 0xD503201F; std::memcpy(t + tramp_offset, &nop, 4); tramp_offset += 4; }
            std::uint32_t ldr_x16 = 0x58000050; std::uint32_t br_x16 = is_bl ? 0xD63F0200 : 0xD61F0200;
            std::memcpy(t + tramp_offset, &ldr_x16, 4); tramp_offset += 4;
            std::memcpy(t + tramp_offset, &br_x16, 4); tramp_offset += 4;
            std::memcpy(t + tramp_offset, &target, 8); tramp_offset += 8;
        };
        auto emit_cond_abs_jump = [&](std::uint32_t insn, int imm_shift, int imm_mask, Address target) {
            if ((tramp_offset % 8) == 0) { std::uint32_t nop = 0xD503201F; std::memcpy(t + tramp_offset, &nop, 4); tramp_offset += 4; }
            std::uint32_t inv_insn = insn ^ (1 << 24); std::uint32_t new_imm = 3; 
            inv_insn = (inv_insn & ~(imm_mask << imm_shift)) | ((new_imm & imm_mask) << imm_shift);
            std::memcpy(t + tramp_offset, &inv_insn, 4); tramp_offset += 4;
            std::uint32_t ldr_x16 = 0x58000050; std::uint32_t br_x16 = 0xD61F0200;
            std::memcpy(t + tramp_offset, &ldr_x16, 4); tramp_offset += 4;
            std::memcpy(t + tramp_offset, &br_x16, 4); tramp_offset += 4;
            std::memcpy(t + tramp_offset, &target, 8); tramp_offset += 8;
        };
        auto emit_ldr_lit_abs = [&](std::uint32_t insn, Address target) {
            int rt = insn & 0x1F;
            if ((tramp_offset % 8) != 0) { std::uint32_t nop = 0xD503201F; std::memcpy(t + tramp_offset, &nop, 4); tramp_offset += 4; }
            std::uint32_t ldr_x16 = 0x58000050; std::uint32_t ldr_rt = 0xF9400200 | rt;
            std::memcpy(t + tramp_offset, &ldr_x16, 4); tramp_offset += 4;
            std::memcpy(t + tramp_offset, &ldr_rt, 4); tramp_offset += 4;
            std::memcpy(t + tramp_offset, &target, 8); tramp_offset += 8;
        };

        {
            SigGuard guard(src_addr, src_addr + kMaxPrologueSize);
            if (guard.jumped()) {
                r.error = HOOK_INVALID_TARGET;
                return r;
            }

            while (copied < min_patch) {
                if (copied + 4 > kMaxPrologueSize) { r.error = HOOK_BOUNDS_EXCEEDED; return r; }

                std::uint32_t insn = read_insn(s + copied);
                auto kind = arm64::decode_kind(insn);
                if (kind == arm64::Kind::Unknown) { r.error = HOOK_RELOCATION_FAILED; return r; }

                std::uint32_t reloced = insn;
                Address insn_addr = src_addr + copied;

                if (kind == arm64::Kind::ADRP) {
                    std::int64_t immlo = (insn >> 29) & 0x3; std::int64_t immhi = (insn >> 5) & 0x7FFFF; std::int64_t imm21 = (immhi << 2) | immlo;
                    if (imm21 & (1LL << 20)) imm21 |= -(1LL << 21);
                    Address orig_target = (insn_addr & ~0xFFFULL) + (imm21 << 12);
                    Address new_adrp_base = (tramp_addr + tramp_offset) & ~0xFFFULL;
                    std::int32_t new_imm21 = static_cast<std::int32_t>((int64_t(orig_target) - int64_t(new_adrp_base)) >> 12);
                    reloced = assemble_adrp(insn, new_imm21);
                    std::memcpy(t + tramp_offset, &reloced, 4); tramp_offset += 4;
                } else if (kind == arm64::Kind::ADR) {
                    std::int64_t immlo = (insn >> 29) & 0x3; std::int64_t immhi = (insn >> 5) & 0x7FFFF; std::int64_t imm21 = (immhi << 2) | immlo;
                    if (imm21 & (1LL << 20)) imm21 |= -(1LL << 21);
                    Address orig_target = insn_addr + imm21; Address new_adr_base = tramp_addr + tramp_offset;
                    std::int32_t new_imm21 = static_cast<std::int32_t>(int64_t(orig_target) - int64_t(new_adr_base));
                    reloced = assemble_adr(insn, new_imm21);
                    std::memcpy(t + tramp_offset, &reloced, 4); tramp_offset += 4;
                } else if (kind == arm64::Kind::LDR_LIT) {
                    std::int64_t imm19 = extract_imm(insn, 5, 19); Address orig_target = insn_addr + (imm19 << 2);
                    Address new_base = tramp_addr + tramp_offset; std::int64_t new_offset = int64_t(orig_target) - int64_t(new_base);
                    if (new_offset % 4 != 0 || new_offset < -(1LL << 20) || new_offset >= (1LL << 20)) { emit_ldr_lit_abs(insn, orig_target); } 
                    else { std::int32_t new_imm19 = static_cast<std::int32_t>(new_offset >> 2) & 0x7FFFF; reloced = (insn & ~(0x7FFFF << 5)) | ((new_imm19 & 0x7FFFF) << 5); std::memcpy(t + tramp_offset, &reloced, 4); tramp_offset += 4; }
                } else if (kind == arm64::Kind::CBZ || kind == arm64::Kind::CBNZ) {
                    std::int64_t imm19 = extract_imm(insn, 5, 19); Address orig_target = insn_addr + (imm19 << 2);
                    Address new_base = tramp_addr + tramp_offset; std::int64_t new_offset = int64_t(orig_target) - int64_t(new_base);
                    if (new_offset % 4 != 0 || new_offset < -(1LL << 20) || new_offset >= (1LL << 20)) { emit_cond_abs_jump(insn, 5, 0x7FFFF, orig_target); } 
                    else { std::int32_t new_imm19 = static_cast<std::int32_t>(new_offset >> 2) & 0x7FFFF; reloced = (insn & ~(0x7FFFF << 5)) | ((new_imm19 & 0x7FFFF) << 5); std::memcpy(t + tramp_offset, &reloced, 4); tramp_offset += 4; }
                } else if (kind == arm64::Kind::TBZ || kind == arm64::Kind::TBNZ) {
                    std::int64_t imm14 = extract_imm(insn, 5, 14); Address orig_target = insn_addr + (imm14 << 2);
                    Address new_base = tramp_addr + tramp_offset; std::int64_t new_offset = int64_t(orig_target) - int64_t(new_base);
                    if (new_offset % 4 != 0 || new_offset < -(1LL << 15) || new_offset >= (1LL << 15)) { emit_cond_abs_jump(insn, 5, 0x3FFF, orig_target); } 
                    else { std::int32_t new_imm14 = static_cast<std::int32_t>(new_offset >> 2) & 0x3FFF; reloced = (insn & ~(0x3FFF << 5)) | ((new_imm14 & 0x3FFF) << 5); std::memcpy(t + tramp_offset, &reloced, 4); tramp_offset += 4; }
                } else if (kind == arm64::Kind::B || kind == arm64::Kind::BL) {
                    std::int64_t imm26 = static_cast<std::int64_t>(insn & 0x03FFFFFF);
                    if (imm26 & (1LL << 25)) imm26 |= -(1LL << 26);
                    Address target_addr = insn_addr + (imm26 << 2);
                    emit_abs_jump(target_addr, kind == arm64::Kind::BL);
                } else { std::memcpy(t + tramp_offset, &insn, 4); tramp_offset += 4; }
                copied += 4;
            }
        }

        arm64::Asm::emit_movz_movk_br(t + tramp_offset, src_addr + copied);
        r.copied = copied; r.tramp_size = tramp_offset + 20; r.error = HOOK_OK;
        return r;
    }
};

// ============================================================================
// GOT HOOKING FALLBACK
// ============================================================================

static bool got_hook_module(void* target, void* replacement, struct dl_phdr_info* info, std::vector<GOTHookEntry>& entries) {
    uintptr_t load_bias = info->dlpi_addr;

    ElfW(Dyn)* dynamic = nullptr;
    for (size_t i = 0; i < info->dlpi_phnum; i++) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dynamic = (ElfW(Dyn)*)(load_bias + info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }
    if (!dynamic) return false;

    ElfW(Rela)* jmprel = nullptr;
    size_t pltrelsz = 0;
    ElfW(Sym)* dynsym = nullptr;

    for (ElfW(Dyn)* d = dynamic; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
            case DT_JMPREL: jmprel = (ElfW(Rela)*)(load_bias + d->d_un.d_ptr); break;
            case DT_PLTRELSZ: pltrelsz = d->d_un.d_val; break;
            case DT_SYMTAB: dynsym = (ElfW(Sym)*)(load_bias + d->d_un.d_ptr); break;
        }
    }

    if (!jmprel || !dynsym || pltrelsz == 0) return false;

    size_t rel_count = pltrelsz / sizeof(ElfW(Rela));
    bool any = false;

    for (size_t i = 0; i < rel_count; i++) {
        ElfW(Rela)* rel = &jmprel[i];
        if (ELF64_R_TYPE(rel->r_info) != R_AARCH64_JUMP_SLOT) continue;

        size_t sym_idx = ELF64_R_SYM(rel->r_info);
        ElfW(Sym)* sym = &dynsym[sym_idx];
        void* sym_addr = (void*)(load_bias + sym->st_value);

        if (sym_addr == target) {
            void* got_entry_addr = (void*)(load_bias + rel->r_offset);
            if (Mem::make_rw(got_entry_addr, sizeof(void*))) {
                void* orig = nullptr;
                std::memcpy(&orig, got_entry_addr, sizeof(void*));
                std::memcpy(got_entry_addr, &replacement, sizeof(void*));
                Mem::flush_caches(got_entry_addr, sizeof(void*));
                entries.push_back({(void**)got_entry_addr, orig});
                any = true;
            }
        }
    }
    return any;
}

struct GOTHookCtx {
    void* target;
    void* replacement;
    std::vector<GOTHookEntry>* entries;
};

static int got_hook_iterate_cb(struct dl_phdr_info* info, size_t size, void* arg) {
    (void)size;
    GOTHookCtx* ctx = (GOTHookCtx*)arg;
    got_hook_module(ctx->target, ctx->replacement, info, *ctx->entries);
    return 0;
}

static bool got_hook_all_modules(void* target, void* replacement, std::vector<GOTHookEntry>& entries) {
    GOTHookCtx ctx = {target, replacement, &entries};
    xdl_iterate_phdr(got_hook_iterate_cb, &ctx, XDL_DEFAULT);
    return !entries.empty();
}

class GOTHookManager {
public:
    static bool install(void* target, void* replacement, std::vector<GOTHookEntry>& entries) {
        return got_hook_all_modules(target, replacement, entries);
    }

    static void remove(std::vector<GOTHookEntry>& entries) {
        for (auto& e : entries) {
            if (e.slot) {
                if (Mem::make_rw(e.slot, sizeof(void*))) {
                    std::memcpy(e.slot, &e.original, sizeof(void*));
                    Mem::flush_caches(e.slot, sizeof(void*));
                }
            }
        }
        entries.clear();
    }
};

class StopTheWorld {
private:
    static std::recursive_mutex global_mu_;
    std::lock_guard<std::recursive_mutex> lock_;
public:
    StopTheWorld() : lock_(global_mu_) {}
    StopTheWorld(const StopTheWorld&) = delete;
    StopTheWorld& operator=(const StopTheWorld&) = delete;
};
std::recursive_mutex StopTheWorld::global_mu_;

struct Hook {
    void* target = nullptr; void* replacement = nullptr; void* trampoline = nullptr;
    std::size_t patch_size = 0; std::size_t tramp_size = 0; std::vector<std::uint8_t> backup;
    bool active = false; bool is_single_insn = false; bool is_got_hook = false;
    std::vector<GOTHookEntry> got_entries;
    int rx_prot = PROT_READ | PROT_EXEC;
};

class HookManager {
public:
    static HookManager& instance() { static HookManager hm; return hm; }

    int install(void* target, void* replacement, void** original_out) {
        StopTheWorld stop;
        std::lock_guard<std::recursive_mutex> lk(mu_);
        if (!target || !replacement) return HOOK_NULL_ARGS;
        if (hooks_.count(target)) return HOOK_ALREADY_HOOKED;

        // --- SYSTEM CRITICAL LIB CHECK ---
        if (is_system_critical_lib(target)) {
            LOGW("[Hook] Target %p is in system critical lib, skipping inline hook, trying GOT fallback...", target);
            std::vector<GOTHookEntry> got_entries;
            if (GOTHookManager::install(target, replacement, got_entries)) {
                maybe_disable_cfi();
                Hook h;
                h.target = target; h.replacement = replacement;
                h.is_got_hook = true; h.got_entries = std::move(got_entries);
                h.active = true;
                hooks_.try_emplace(target, std::move(h));
                if (original_out) *original_out = target;
                LOGI("[Hook] GOT hook installed successfully for target=%p", target);
                return HOOK_OK;
            }
            LOGE("[Hook] GOT hook failed for critical lib target=%p", target);
            return HOOK_MPROTECT_FAILED;
        }

        // --- NON-EXECUTABLE MEMORY CHECK (PairIP Bypass) ---
        if (!is_executable_region(target)) {
            LOGW("[Hook] Target %p is not in executable memory (r-x). Trying GOT fallback...", target);
            std::vector<GOTHookEntry> got_entries;
            if (GOTHookManager::install(target, replacement, got_entries)) {
                maybe_disable_cfi();
                Hook h;
                h.target = target; h.replacement = replacement;
                h.is_got_hook = true; h.got_entries = std::move(got_entries);
                h.active = true;
                hooks_.try_emplace(target, std::move(h));
                if (original_out) *original_out = target;
                LOGI("[Hook] GOT hook installed successfully for non-exec target=%p", target);
                return HOOK_OK;
            }
            LOGE("[Hook] GOT hook failed for non-exec target=%p", target);
            return HOOK_INVALID_TARGET;
        }

        ExecMemGuard tramp_guard(kTrampolineSize);
        void* tramp = tramp_guard.get();
        if (!tramp) return HOOK_ALLOC_FAILED;

        auto rel = Relocator::relocate(target, tramp, kMinPatchSize);
        if (rel.error == HOOK_OK) {
            Hook h;
            h.target = target; h.replacement = replacement; h.trampoline = tramp;
            h.patch_size = rel.copied; h.tramp_size = rel.tramp_size; h.backup.resize(h.patch_size);
            std::memcpy(h.backup.data(), target, h.patch_size);

            std::vector<std::uint8_t> full_patch(h.patch_size);
            arm64::Asm::emit_movz_movk_br(full_patch.data(), reinterpret_cast<Address>(replacement));
            for (std::size_t off = kMinPatchSize; off < h.patch_size; off += 4) {
                std::uint32_t nop = 0xD503201F; 
                std::memcpy(full_patch.data() + off, &nop, 4);
            }

            bool patched = false;

            if (Mem::make_rw(target, h.patch_size)) {
                atomic_write_inst(target, full_patch.data(), h.patch_size);
                Mem::flush_caches(target, h.patch_size);
                if (Mem::make_rx(target, h.patch_size)) {
                    patched = true;
                } else {
                    atomic_write_inst(target, h.backup.data(), h.patch_size);
                    Mem::flush_caches(target, h.patch_size);
                    Mem::page_protect(target, h.patch_size, PROT_READ | PROT_EXEC);
                    LOGW("[Hook] mprotect to RX failed. Falling back to /proc/self/mem.");
                }
            }

            if (!patched) {
                if (write_mem_proc(target, full_patch.data(), h.patch_size)) {
                    Mem::flush_caches(target, h.patch_size);
                    patched = true;
                    LOGI("[Hook] Patched successfully via /proc/self/mem.");
                }
            }

            if (!patched) {
                LOGE("[Hook] Failed to patch target %p", target);
                return HOOK_MPROTECT_FAILED;
            }

            if (!Mem::make_rx(tramp, kTrampolineSize)) {
                write_mem_proc(target, h.backup.data(), h.patch_size);
                Mem::flush_caches(target, h.patch_size);
                return HOOK_MPROTECT_FAILED;
            }

            maybe_disable_cfi();
            h.active = true;
            hooks_.try_emplace(target, std::move(h));
            if (original_out) *original_out = tramp;
            tramp_guard.release();
            return HOOK_OK;
        }

        LOGW("[Hook] Inline hook failed (err=%d), attempting GOT hook fallback...", rel.error);
        std::vector<GOTHookEntry> got_entries;
        if (GOTHookManager::install(target, replacement, got_entries)) {
            maybe_disable_cfi();
            Hook h;
            h.target = target; h.replacement = replacement;
            h.is_got_hook = true; h.got_entries = std::move(got_entries);
            h.active = true;
            hooks_.try_emplace(target, std::move(h));
            if (original_out) *original_out = target;
            LOGI("[Hook] GOT hook installed successfully for target=%p", target);
            return HOOK_OK;
        }

        return rel.error;
    }

    int install_single_insn(void* target, void* replacement, void** original_out) {
        StopTheWorld stop;
        std::lock_guard<std::recursive_mutex> lk(mu_);
        if (!target || !replacement) return HOOK_NULL_ARGS;
        if (hooks_.count(target)) return HOOK_ALREADY_HOOKED;
        if (original_out != nullptr) return install(target, replacement, original_out);

        if (is_system_critical_lib(target) || !is_executable_region(target)) {
            return install(target, replacement, original_out);
        }

        std::int64_t offset = reinterpret_cast<std::int64_t>(replacement) - reinterpret_cast<std::int64_t>(target);
        if (offset >= -(1LL << 27) && offset < (1LL << 27)) {
            Hook h; h.target = target; h.replacement = replacement; h.patch_size = 4; h.is_single_insn = true; h.backup.resize(4);
            std::memcpy(h.backup.data(), target, 4);
            
            std::uint32_t imm26 = (static_cast<std::uint32_t>(offset >> 2)) & 0x03FFFFFF; 
            std::uint32_t insn = 0x14000000 | imm26;

            bool patched = false;
            if (Mem::make_rw(target, 4)) {
                atomic_write_inst(target, &insn, 4); 
                Mem::flush_caches(target, 4);
                if (Mem::make_rx(target, 4)) {
                    patched = true;
                } else {
                    atomic_write_inst(target, h.backup.data(), 4);
                    Mem::flush_caches(target, 4);
                    Mem::page_protect(target, 4, PROT_READ | PROT_EXEC);
                }
            }
            if (!patched) {
                if (write_mem_proc(target, &insn, 4)) {
                    Mem::flush_caches(target, 4);
                    patched = true;
                    LOGI("[Hook] Single insn patched via /proc/self/mem.");
                }
            }
            if (!patched) return HOOK_MPROTECT_FAILED;

            maybe_disable_cfi();
            h.active = true; hooks_.try_emplace(target, std::move(h)); return HOOK_OK;
        }
        return install(target, replacement, original_out);
    }

    int remove(void* target) {
        StopTheWorld stop;
        std::lock_guard<std::recursive_mutex> lk(mu_);
        auto it = hooks_.find(target); if (it == hooks_.end()) return HOOK_INVALID_TARGET;
        Hook& h = it->second; int result = HOOK_OK;
        if (h.active && h.target) {
            if (h.is_got_hook) {
                GOTHookManager::remove(h.got_entries);
            } else if (!h.backup.empty()) {
                bool restored = false;
                if (Mem::make_rw(h.target, h.backup.size())) {
                    atomic_write_inst(h.target, h.backup.data(), h.backup.size());
                    Mem::flush_caches(h.target, h.backup.size());
                    if (Mem::page_protect(h.target, h.backup.size(), h.rx_prot)) restored = true;
                }
                if (!restored) {
                    if (write_mem_proc(h.target, h.backup.data(), h.backup.size())) {
                        Mem::flush_caches(h.target, h.backup.size());
                        restored = true;
                    }
                }
                if (!restored) result = HOOK_MPROTECT_FAILED;
            }
            h.active = false;
        }
        if (h.trampoline && !h.is_got_hook && !h.is_single_insn) syscall(SYS_munmap, h.trampoline, kTrampolineSize);
        hooks_.erase(it);
        maybe_restore_cfi();
        return result;
    }

    void* trampoline_for(void* target) {
        std::lock_guard<std::recursive_mutex> lk(mu_);
        auto it = hooks_.find(target); if (it == hooks_.end()) return nullptr;
        return it->second.trampoline;
    }
private:
    std::recursive_mutex mu_; std::unordered_map<void*, Hook> hooks_;
};

static void* hooked_dlopen(const char* filename, int flags) {
    typedef void* (*dlopen_fn)(const char*, int);
    void* result = ((dlopen_fn)orig_dlopen_ptr)(filename, flags);
    if (result && filename) {
        apply_pending_hooks_for_lib(filename);
    }
    return result;
}

#if defined(__ANDROID_API__) && __ANDROID_API__ >= 21
static void* hooked_android_dlopen_ext(const char* filename, int flags,
                                       const android_dlextinfo* info, void* caller_addr) {
    typedef void* (*android_dlopen_ext_fn)(const char*, int, const android_dlextinfo*, void*);
    void* result = ((android_dlopen_ext_fn)orig_android_dlopen_ext_ptr)(filename, flags, info, caller_addr);
    if (result && filename) {
        apply_pending_hooks_for_lib(filename);
    }
    return result;
}
#endif

static void apply_pending_hooks_for_lib(const char* loaded_lib_name) {
    if (!loaded_lib_name) return;

    std::vector<PendingHook> to_process;
    {
        std::lock_guard<std::mutex> lk(g_pending_mu);
        for (auto it = g_pending_hooks.begin(); it != g_pending_hooks.end(); ) {
            if (it->lib_name.find(loaded_lib_name) != std::string::npos ||
                std::string(loaded_lib_name).find(it->lib_name) != std::string::npos) {
                to_process.push_back(*it);
                it = g_pending_hooks.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (auto& p : to_process) {
        void* handle = xdl_open(loaded_lib_name, XDL_DEFAULT);
        if (!handle) continue;
        void* target = xdl_sym(handle, p.sym_name.c_str(), nullptr);
        if (!target) target = xdl_dsym(handle, p.sym_name.c_str(), nullptr);
        xdl_close(handle);

        if (target) {
            void* trampoline = nullptr;
            int err = HookManager::instance().install(target, p.replacement, &trampoline);
            if (err == HOOK_OK && p.original_out) {
                *p.original_out = trampoline;
            }
        } else {
            LOGW("[Pending] Symbol %s not found in %s", p.sym_name.c_str(), loaded_lib_name);
        }
    }
}

static void init_dlopen_monitor() {
    bool expected = false;
    if (!g_dlopen_hooked.compare_exchange_strong(expected, true)) return;

    void* dlopen_addr = nullptr;
    void* android_dlopen_ext_addr = nullptr;

    void* libdl = xdl_open("libdl.so", XDL_DEFAULT);
    if (libdl) {
        dlopen_addr = xdl_sym(libdl, "dlopen", nullptr);
#if defined(__ANDROID_API__) && __ANDROID_API__ >= 21
        android_dlopen_ext_addr = xdl_sym(libdl, "android_dlopen_ext", nullptr);
#endif
        xdl_close(libdl);
    }

    if (dlopen_addr) {
        void* trampoline = nullptr;
        int err = HookManager::instance().install(dlopen_addr, (void*)hooked_dlopen, &trampoline);
        if (err == HOOK_OK) {
            orig_dlopen_ptr = trampoline;
            LOGI("[Pending] dlopen monitor installed.");
        } else {
            LOGW("[Pending] Inline dlopen hook failed (%d), trying GOT fallback...", err);
            if (got_hook_all_modules(dlopen_addr, (void*)hooked_dlopen, g_dlopen_got_entries)) {
                LOGI("[Pending] dlopen GOT monitor installed (%zu entries).", g_dlopen_got_entries.size());
            }
        }
    }

#if defined(__ANDROID_API__) && __ANDROID_API__ >= 21
    if (android_dlopen_ext_addr) {
        void* trampoline = nullptr;
        int err = HookManager::instance().install(android_dlopen_ext_addr, (void*)hooked_android_dlopen_ext, &trampoline);
        if (err == HOOK_OK) {
            orig_android_dlopen_ext_ptr = trampoline;
            LOGI("[Pending] android_dlopen_ext monitor installed.");
        } else {
            LOGW("[Pending] Inline android_dlopen_ext hook failed (%d), trying GOT fallback...", err);
            std::vector<GOTHookEntry> entries;
            if (got_hook_all_modules(android_dlopen_ext_addr, (void*)hooked_android_dlopen_ext, entries)) {
                LOGI("[Pending] android_dlopen_ext GOT monitor installed (%zu entries).", entries.size());
            }
        }
    }
#endif
}

} // namespace sandhook

extern "C" {

int sandhook_install_ex(void* target, void* replacement, void** original_out) {
    sandhook::init_dlopen_monitor();
    return sandhook::HookManager::instance().install(target, replacement, original_out);
}

void* sandhook_install(void* target, void* replacement, void** original_out) {
    sandhook::init_dlopen_monitor();
    int err = sandhook::HookManager::instance().install(target, replacement, original_out);
    return (err == HOOK_OK) ? target : nullptr;
}

int sandhook_install_single_insn(void* target, void* replacement, void** original_out) {
    sandhook::init_dlopen_monitor();
    return sandhook::HookManager::instance().install_single_insn(target, replacement, original_out);
}

int sandhook_remove(void* target) {
    return sandhook::HookManager::instance().remove(target);
}

void* sandhook_trampoline(void* target) {
    return sandhook::HookManager::instance().trampoline_for(target);
}

const char* sandhook_version() {
    return "sandhook-arm64-production-4.8";
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
        case HOOK_OUT_OF_RANGE: return "OUT_OF_RANGE";
        default: return "UNKNOWN_ERROR";
    }
}

int sandhook_install_pending(const char* lib_name, const char* sym_name, void* replacement, void** original_out) {
    if (!lib_name || !sym_name || !replacement) return HOOK_NULL_ARGS;

    void* handle = xdl_open(lib_name, XDL_DEFAULT);
    if (handle) {
        void* target = xdl_sym(handle, sym_name, nullptr);
        if (!target) target = xdl_dsym(handle, sym_name, nullptr);
        xdl_close(handle);
        if (target) {
            return sandhook_install_ex(target, replacement, original_out);
        }
    }

    {
        std::lock_guard<std::mutex> lk(sandhook::g_pending_mu);
        for (const auto& p : sandhook::g_pending_hooks) {
            if (p.lib_name == lib_name && p.sym_name == sym_name) {
                return HOOK_ALREADY_HOOKED;
            }
        }
        sandhook::g_pending_hooks.push_back({lib_name, sym_name, replacement, original_out});
    }

    sandhook::init_dlopen_monitor();
    return HOOK_OK;
}

} // extern "C"