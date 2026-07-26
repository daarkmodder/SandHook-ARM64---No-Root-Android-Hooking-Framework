#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <sys/mman.h>
#include <signal.h>
#include <setjmp.h>
#include <link.h>
#include <elf.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <android/log.h>
#include "../obfuscate.h"
#include "../public/sandhook.h"
#include "../xdl/xdl.h"

#define LOG_TAG "SandHook-Prod"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

#ifndef PROT_BTI
#define PROT_BTI 0x10
#endif

namespace sandhook {

constexpr std::size_t kPageSize = 4096;
constexpr std::size_t kMinPatchSize = 20;
constexpr std::size_t kTrampolineSize = 512;
constexpr std::size_t kMaxPrologueSize = 200;

typedef std::uintptr_t Address;

struct GOTHookEntry { void** slot; void* original; };

// --- MEMORY MODULE ---
namespace Mem {
    bool page_protect(void* addr, std::size_t len, int prot);
    bool make_rw(void* addr, std::size_t len);
    bool make_rx(void* addr, std::size_t len);
    void flush_caches(void* addr, std::size_t len);
}
bool write_mem_proc(void* addr, const void* data, size_t len);
bool is_system_critical_lib(void* target);
bool is_executable_region(void* target);
void atomic_write_inst(void* target, const void* inst, size_t len);
bool read_proc_maps_syscall(std::string& out);

// --- SIG GUARD MODULE ---
class SigGuard {
    sigjmp_buf jmpbuf_;
    bool jumped_ = false;
public:
    SigGuard(uintptr_t range_start, uintptr_t range_end);
    ~SigGuard();
    bool jumped() const { return jumped_; }
};

// --- ARM64 MODULE (Renombrado a Inst) ---
namespace arm64 {
    namespace Inst {
        void emit_movz_movk_br(std::uint8_t* out, Address target);
        void fill_nops(std::uint8_t* out, std::size_t start, std::size_t end);
    }
}
struct Relocator {
    struct Result { std::size_t copied = 0; std::size_t tramp_size = 0; int error = HOOK_OK; };
    // FIX: Añadido max_size para limitar el escaneo según el tamaño real del símbolo
    static Result relocate(void* src, void* tramp, std::size_t min_patch = kMinPatchSize, std::size_t max_size = kMaxPrologueSize);
};

// --- GOT MODULE ---
bool got_hook_all_modules(void* target, void* replacement, std::vector<GOTHookEntry>& entries);

// --- PROTECTIONS MODULE ---
void maybe_disable_cfi();
void maybe_restore_cfi();
bool has_cfi_bypass_failed();
bool install_ret_patch(void* target, int64_t return_value_x0 = 0, bool set_x0 = false);
bool remove_ret_patch(void* target);

// --- UTILS MODULE ---
uintptr_t find_pattern_in_lib(const char* lib_name, const char* pattern);
void* get_cached_xdl_handle(const char* lib_name);
bool write_nop_internal(void* addr, int count);
bool write_ret_internal(void* addr);

} // namespace sandhook