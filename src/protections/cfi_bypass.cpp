#include "../internal/sandhook_internal.h"

namespace sandhook {

#define CFI_ARM64_RET_INST 0xd65f03c0
static bool g_cfi_disabled = false;
static bool g_cfi_bypass_failed = false; 
static uint32_t g_cfi_slowpath_backup = 0;
static uint32_t g_cfi_slowpath_diag_backup = 0;
static std::mutex g_cfi_mu;
static std::atomic<int> g_active_hook_count{0};

bool has_cfi_bypass_failed() {
    if (g_cfi_bypass_failed) return true;
    const char* status_path = AY_OBFUSCATE("/proc/self/status");
    FILE* fp = fopen(status_path, "r");
    if (!fp) return false;
    char line[256]; bool cfi_enabled = false; const char* cfi_tag = AY_OBFUSCATE("CFI:");
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, cfi_tag)) { int val = 0; if (sscanf(line, "CFI:\t%d", &val) == 1) { cfi_enabled = (val == 1); break; } }
    }
    fclose(fp); return cfi_enabled;
}

static void disableCFISlowpath() {
    std::lock_guard<std::mutex> lk(g_cfi_mu);
    if (g_cfi_disabled || g_cfi_bypass_failed) return;
    void* slowpath = nullptr; void* slowpath_diag = nullptr;
    const char* lib_names[] = {AY_OBFUSCATE("libc.so"), AY_OBFUSCATE("libdl.so"), AY_OBFUSCATE("linker64"), nullptr};
    const char* sym1 = AY_OBFUSCATE("__cfi_slowpath"); const char* sym2 = AY_OBFUSCATE("__cfi_slowpath_diag");
    for (int i = 0; lib_names[i] != nullptr; ++i) {
        void* handle = xdl_open(lib_names[i], XDL_DEFAULT); if (!handle) continue;
        slowpath = xdl_sym(handle, sym1, nullptr); if (!slowpath) slowpath = xdl_dsym(handle, sym1, nullptr);
        slowpath_diag = xdl_sym(handle, sym2, nullptr); if (!slowpath_diag) slowpath_diag = xdl_dsym(handle, sym2, nullptr);
        xdl_close(handle); if (slowpath && slowpath_diag) break;
    }
    if (slowpath && slowpath_diag) {
        uint32_t ret_inst = CFI_ARM64_RET_INST;
        std::memcpy(&g_cfi_slowpath_backup, slowpath, sizeof(uint32_t));
        std::memcpy(&g_cfi_slowpath_diag_backup, slowpath_diag, sizeof(uint32_t));
        if (write_mem_proc(slowpath, &ret_inst, sizeof(uint32_t)) && write_mem_proc(slowpath_diag, &ret_inst, sizeof(uint32_t))) {
            __builtin___clear_cache((char*)slowpath, (char*)((uintptr_t)slowpath + sizeof(uint32_t)));
            __builtin___clear_cache((char*)slowpath_diag, (char*)((uintptr_t)slowpath_diag + sizeof(uint32_t)));
            g_cfi_disabled = true; LOGI("[CFI] Slowpath disabled via /proc/self/mem. Backups saved.");
        } else {
            write_mem_proc(slowpath, &g_cfi_slowpath_backup, sizeof(uint32_t)); write_mem_proc(slowpath_diag, &g_cfi_slowpath_diag_backup, sizeof(uint32_t));
            g_cfi_bypass_failed = true; LOGE("[CFI] Failed to patch. CFI remains active.");
        }
    } else { g_cfi_bypass_failed = true; LOGE("[CFI] Could not find __cfi_slowpath via xDL."); }
}

void maybe_disable_cfi() { if (g_active_hook_count.fetch_add(1) == 0) disableCFISlowpath(); }

// FIX: Restauración de CFI implementada correctamente
void maybe_restore_cfi() {
    if (g_active_hook_count.fetch_sub(1) == 1) {
        std::lock_guard<std::mutex> lk(g_cfi_mu);
        if (!g_cfi_disabled) return;
        
        void* slowpath = nullptr; void* slowpath_diag = nullptr;
        const char* lib_names[] = {AY_OBFUSCATE("libc.so"), AY_OBFUSCATE("libdl.so"), AY_OBFUSCATE("linker64"), nullptr};
        const char* sym1 = AY_OBFUSCATE("__cfi_slowpath"); const char* sym2 = AY_OBFUSCATE("__cfi_slowpath_diag");
        for (int i = 0; lib_names[i] != nullptr; ++i) {
            void* handle = xdl_open(lib_names[i], XDL_DEFAULT); if (!handle) continue;
            slowpath = xdl_sym(handle, sym1, nullptr); if (!slowpath) slowpath = xdl_dsym(handle, sym1, nullptr);
            slowpath_diag = xdl_sym(handle, sym2, nullptr); if (!slowpath_diag) slowpath_diag = xdl_dsym(handle, sym2, nullptr);
            xdl_close(handle); if (slowpath && slowpath_diag) break;
        }

        if (slowpath && slowpath_diag) {
            if (write_mem_proc(slowpath, &g_cfi_slowpath_backup, sizeof(uint32_t)) && 
                write_mem_proc(slowpath_diag, &g_cfi_slowpath_diag_backup, sizeof(uint32_t))) {
                __builtin___clear_cache((char*)slowpath, (char*)((uintptr_t)slowpath + sizeof(uint32_t)));
                __builtin___clear_cache((char*)slowpath_diag, (char*)((uintptr_t)slowpath_diag + sizeof(uint32_t)));
                g_cfi_disabled = false;
                LOGI("[CFI] Slowpath restored successfully from backup.");
            } else {
                LOGE("[CFI] Failed to restore slowpath!");
            }
        }
    }
}

} // namespace sandhook