#include "../internal/sandhook_internal.h"
#include <unistd.h>
#include <sys/syscall.h>
#include <android/dlext.h>

namespace sandhook {

class ExecMemGuard {
    void* ptr_ = nullptr; std::size_t size_ = 0;
public:
    ExecMemGuard() = default;
    explicit ExecMemGuard(std::size_t size) : size_(size) {
        ptr_ = (void*)syscall(SYS_mmap, nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (!ptr_ || ptr_ == MAP_FAILED) { ptr_ = nullptr; }
    }
    ~ExecMemGuard() { if (ptr_) syscall(SYS_munmap, ptr_, size_); }
    ExecMemGuard(const ExecMemGuard&) = delete; ExecMemGuard& operator=(const ExecMemGuard&) = delete;
    ExecMemGuard(ExecMemGuard&& o) noexcept : ptr_(o.ptr_), size_(o.size_) { o.ptr_ = nullptr; o.size_ = 0; }
    void* get() { return ptr_; } void release() { ptr_ = nullptr; size_ = 0; }
};

class StopTheWorld {
    static std::recursive_mutex global_mu_; std::lock_guard<std::recursive_mutex> lock_;
public: StopTheWorld() : lock_(global_mu_) {}
};
std::recursive_mutex StopTheWorld::global_mu_;

struct Hook {
    void* target = nullptr; void* replacement = nullptr; void* trampoline = nullptr;
    std::size_t patch_size = 0; std::size_t tramp_size = 0; std::vector<std::uint8_t> backup;
    bool active = false; bool is_single_insn = false; bool is_got_hook = false; bool is_ret_patch = false;
    std::vector<GOTHookEntry> got_entries; int rx_prot = PROT_READ | PROT_EXEC;
};

class HookManager {
    std::recursive_mutex mu_; std::unordered_map<void*, Hook> hooks_;
public:
    static HookManager& instance() { static HookManager hm; return hm; }

    int install(void* target, void* replacement, void** original_out) {
        StopTheWorld stop; std::lock_guard<std::recursive_mutex> lk(mu_);
        if (!target || !replacement) return HOOK_NULL_ARGS;
        if (hooks_.count(target)) return HOOK_ALREADY_HOOKED;

        if (has_cfi_bypass_failed()) {
            std::vector<GOTHookEntry> got_entries;
            if (got_hook_all_modules(target, replacement, got_entries)) {
                Hook h; h.target = target; h.replacement = replacement; h.is_got_hook = true; h.got_entries = std::move(got_entries); h.active = true;
                hooks_.try_emplace(target, std::move(h)); if (original_out) *original_out = target; return HOOK_OK;
            }
            if (install_ret_patch(target, 0, false)) {
                Hook h; h.target = target; h.replacement = replacement; h.is_ret_patch = true; h.active = true;
                hooks_.try_emplace(target, std::move(h)); if (original_out) *original_out = nullptr; return HOOK_OK;
            }
            return HOOK_PROTECTED;
        }

        if (!is_executable_region(target) || is_system_critical_lib(target)) {
            std::vector<GOTHookEntry> got_entries;
            if (got_hook_all_modules(target, replacement, got_entries)) {
                maybe_disable_cfi(); Hook h; h.target = target; h.replacement = replacement; h.is_got_hook = true; h.got_entries = std::move(got_entries); h.active = true;
                hooks_.try_emplace(target, std::move(h)); if (original_out) *original_out = target; return HOOK_OK;
            }
            if (install_ret_patch(target, 0, false)) {
                Hook h; h.target = target; h.replacement = replacement; h.is_ret_patch = true; h.active = true;
                hooks_.try_emplace(target, std::move(h)); if (original_out) *original_out = nullptr; return HOOK_OK;
            }
            return HOOK_INVALID_TARGET;
        }

        // ====================================================================
        // BLOQUE INLINE HOOK (Mejorado con sym_size de GlossHook y Dobby LDR)
        // ====================================================================
        std::size_t max_reloc_size = kMaxPrologueSize; 
        xdl_info_t info;
        void* cache = nullptr;
        if (xdl_addr4(target, &info, &cache, XDL_DEFAULT)) {
            if (info.dli_ssize > 0 && info.dli_ssize <= kMaxPrologueSize) {
                max_reloc_size = info.dli_ssize; 
            }
        }
        xdl_addr_clean(&cache);

        ExecMemGuard tramp_guard(kTrampolineSize); 
        void* tramp = tramp_guard.get();
        if (!tramp) return HOOK_ALLOC_FAILED;
        
        auto rel = Relocator::relocate(target, tramp, kMinPatchSize, max_reloc_size);
        
        if (rel.error == HOOK_OK) {
            Hook h; h.target = target; h.replacement = replacement; h.trampoline = tramp;
            h.patch_size = rel.copied; h.tramp_size = rel.tramp_size; h.backup.resize(h.patch_size); 
            std::memcpy(h.backup.data(), target, h.patch_size);
            
            std::vector<std::uint8_t> full_patch(h.patch_size);
            arm64::Inst::emit_ldr_br(full_patch.data(), reinterpret_cast<Address>(replacement));
            arm64::Inst::fill_nops(full_patch.data(), kMinPatchSize, h.patch_size);
            
            if (Mem::make_rw(target, h.patch_size)) {
                atomic_write_inst(target, full_patch.data(), h.patch_size); 
                Mem::flush_caches(target, h.patch_size);
                if (Mem::make_rx(target, h.patch_size) && Mem::make_rx(tramp, kTrampolineSize)) {
                    maybe_disable_cfi(); h.active = true; hooks_.try_emplace(target, std::move(h));
                    if (original_out) *original_out = tramp; tramp_guard.release(); return HOOK_OK;
                }
                atomic_write_inst(target, h.backup.data(), h.patch_size); 
                Mem::flush_caches(target, h.patch_size); 
                Mem::make_rx(target, h.patch_size);
            }
            
            std::vector<GOTHookEntry> got_entries;
            if (got_hook_all_modules(target, replacement, got_entries)) {
                maybe_disable_cfi(); 
                Hook h_got; h_got.target = target; h_got.replacement = replacement; h_got.is_got_hook = true; h_got.got_entries = std::move(got_entries); h_got.active = true;
                hooks_.try_emplace(target, std::move(h_got)); 
                if (original_out) *original_out = target; return HOOK_OK;
            }
            if (install_ret_patch(target, 0, false)) {
                Hook h_ret; h_ret.target = target; h_ret.replacement = replacement; h_ret.is_ret_patch = true; h_ret.active = true;
                hooks_.try_emplace(target, std::move(h_ret)); 
                if (original_out) *original_out = nullptr; return HOOK_OK;
            }
            return HOOK_MPROTECT_FAILED;
        }
        return rel.error;
    }

    int remove(void* target) {
        StopTheWorld stop; std::lock_guard<std::recursive_mutex> lk(mu_);
        auto it = hooks_.find(target); if (it == hooks_.end()) return HOOK_INVALID_TARGET;
        Hook& h = it->second; int result = HOOK_OK;
        if (h.active && h.target) {
            if (h.is_got_hook) {
                for (auto& e : h.got_entries) if (Mem::make_rw(e.slot, sizeof(void*))) { std::memcpy(e.slot, &e.original, sizeof(void*)); Mem::flush_caches(e.slot, sizeof(void*)); }
            } else if (h.is_ret_patch) { remove_ret_patch(h.target); }
            else if (!h.backup.empty()) {
                if (Mem::make_rw(h.target, h.backup.size())) {
                    atomic_write_inst(h.target, h.backup.data(), h.backup.size()); Mem::flush_caches(h.target, h.backup.size()); Mem::make_rx(h.target, h.backup.size());
                } else result = HOOK_MPROTECT_FAILED;
            }
            h.active = false;
        }
        if (h.trampoline && !h.is_got_hook && !h.is_ret_patch) syscall(SYS_munmap, h.trampoline, kTrampolineSize);
        hooks_.erase(it); maybe_restore_cfi(); return result;
    }
};

struct PendingHook { std::string lib_name; std::string sym_name; void* replacement; void** original_out; };
static std::vector<PendingHook> g_pending_hooks; static std::mutex g_pending_mu;

static void apply_pending_hooks_for_lib(const char* loaded_lib_name) {
    if (!loaded_lib_name) return; std::vector<PendingHook> to_process;
    { std::lock_guard<std::mutex> lk(g_pending_mu);
      for (auto it = g_pending_hooks.begin(); it != g_pending_hooks.end(); ) {
          if (it->lib_name.find(loaded_lib_name) != std::string::npos) { to_process.push_back(*it); it = g_pending_hooks.erase(it); } else ++it;
      }}
    for (auto& p : to_process) {
        void* handle = get_cached_xdl_handle(loaded_lib_name); 
        if (!handle) continue;
        void* target = xdl_sym(handle, p.sym_name.c_str(), nullptr); 
        if (!target) target = xdl_dsym(handle, p.sym_name.c_str(), nullptr);
        if (target) { void* trampoline = nullptr; HookManager::instance().install(target, p.replacement, &trampoline); if (p.original_out) *p.original_out = trampoline; }
    }
}

} // namespace sandhook

extern "C" {

int sandhook_install_ex(void* target, void* replacement, void** original_out) { return sandhook::HookManager::instance().install(target, replacement, original_out); }
int sandhook_remove(void* target) { return sandhook::HookManager::instance().remove(target); }
int sandhook_ret_patch(void* target, int64_t return_value, int use_return_value) { return sandhook::install_ret_patch(target, return_value, use_return_value != 0) ? HOOK_OK : HOOK_PROTECTED; }

const char* sandhook_version() { return "sandhook-arm64-production-6.0"; }
const char* sandhook_error_string(int err) {
    switch (err) {
        case HOOK_OK: return "OK"; case HOOK_NULL_ARGS: return "NULL_ARGS"; case HOOK_ALREADY_HOOKED: return "ALREADY_HOOKED";
        case HOOK_RELOCATION_FAILED: return "RELOCATION_FAILED"; case HOOK_MPROTECT_FAILED: return "MPROTECT_FAILED";
        case HOOK_ALLOC_FAILED: return "ALLOC_FAILED"; case HOOK_INVALID_TARGET: return "INVALID_TARGET";
        case HOOK_BOUNDS_EXCEEDED: return "BOUNDS_EXCEEDED"; case HOOK_OUT_OF_RANGE: return "OUT_OF_RANGE";
        case HOOK_ERR_PAC: return "PAC_FAILED"; case HOOK_PENDING: return "PENDING (Lib not loaded yet)";
        case HOOK_PROTECTED: return "PROTECTED (CFI/SELinux active)"; default: return "UNKNOWN_ERROR";
    }
}

int sandhook_install_pending(const char* lib_name, const char* sym_name, void* replacement, void** original_out) {
    if (!lib_name || !sym_name || !replacement) return HOOK_NULL_ARGS;
    void* handle = sandhook::get_cached_xdl_handle(lib_name);
    if (handle) {
        void* target = xdl_sym(handle, sym_name, nullptr); if (!target) target = xdl_dsym(handle, sym_name, nullptr);
        if (target) return sandhook_install_ex(target, replacement, original_out);
    }
    { std::lock_guard<std::mutex> lk(sandhook::g_pending_mu);
      for (const auto& p : sandhook::g_pending_hooks) if (p.lib_name == lib_name && p.sym_name == sym_name) return HOOK_ALREADY_HOOKED;
      sandhook::g_pending_hooks.push_back({lib_name, sym_name, replacement, original_out}); }
    return HOOK_PENDING;
}

} // extern "C"