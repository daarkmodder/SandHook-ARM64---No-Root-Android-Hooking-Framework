#include "../internal/sandhook_internal.h"

namespace sandhook {

#define ARM64_RET_INSN 0xD65F03C0
struct RetPatchEntry { void* target; std::vector<uint8_t> backup; };
static std::vector<RetPatchEntry> g_ret_patches;
static std::mutex g_ret_patch_mu;

bool install_ret_patch(void* target, int64_t return_value_x0, bool set_x0) {
    if (!target) return false;
    if (is_system_critical_lib(target)) return false;

    std::lock_guard<std::mutex> lk(g_ret_patch_mu);
    for (auto& e : g_ret_patches) if (e.target == target) return true;

    size_t patch_size = set_x0 ? 20 : 4; 
    RetPatchEntry entry; 
    entry.target = target; 
    entry.backup.resize(patch_size);
    
    { 
        SigGuard guard((uintptr_t)target, (uintptr_t)target + patch_size); 
        if (guard.jumped()) return false; 
        std::memcpy(entry.backup.data(), target, patch_size); 
    }

    std::vector<uint8_t> patch(patch_size, 0);
    if (set_x0) {
        uint8_t* p = patch.data();
        uint32_t movz = 0xD2800000 | ((return_value_x0 & 0xFFFF) << 5); std::memcpy(p, &movz, 4); p += 4;
        uint32_t movk1 = 0xF2A00000 | (((return_value_x0 >> 16) & 0xFFFF) << 5); std::memcpy(p, &movk1, 4); p += 4;
        uint32_t movk2 = 0xF2C00000 | (((return_value_x0 >> 32) & 0xFFFF) << 5); std::memcpy(p, &movk2, 4); p += 4;
        uint32_t movk3 = 0xF2E00000 | (((return_value_x0 >> 48) & 0xFFFF) << 5); std::memcpy(p, &movk3, 4); p += 4;
        uint32_t ret = ARM64_RET_INSN; std::memcpy(p, &ret, 4);
    } else { 
        uint32_t ret = ARM64_RET_INSN; std::memcpy(patch.data(), &ret, 4); 
    }

    bool patched = false;

    // ESTRATEGIA 1: /proc/self/mem
    if (write_mem_proc(target, patch.data(), patch_size)) { 
        Mem::flush_caches(target, patch_size); 
        patched = true; 
        LOGI("[RetPatch] Escritura exitosa via /proc/self/mem en %p", target);
    } 
    
    // ESTRATEGIA 2: mprotect + Dobby MAP_FIXED
    if (!patched) {
        LOGW("[RetPatch] /proc/self/mem falló, intentando mprotect+Dobby en %p", target);
        if (Mem::make_rw(target, patch_size)) {
            std::memcpy(target, patch.data(), patch_size); 
            Mem::flush_caches(target, patch_size);
            
            if (Mem::make_rx(target, patch_size)) {
                patched = true;
            } else { 
                std::memcpy(target, entry.backup.data(), patch_size); 
                Mem::flush_caches(target, patch_size); 
                Mem::make_rx(target, patch_size); 
                LOGE("[RetPatch] Falló incluso con Dobby MAP_FIXED en %p. Abortando.", target);
            }
        }
    }

    if (patched) { 
        g_ret_patches.push_back(std::move(entry)); 
        LOGI("[RetPatch] Parche RET instalado en %p", target); 
        return true; 
    }
    return false;
}

bool remove_ret_patch(void* target) {
    std::lock_guard<std::mutex> lk(g_ret_patch_mu);
    for (auto it = g_ret_patches.begin(); it != g_ret_patches.end(); ++it) {
        if (it->target == target) {
            if (Mem::make_rw(it->target, it->backup.size())) {
                std::memcpy(it->target, it->backup.data(), it->backup.size()); 
                Mem::flush_caches(it->target, it->backup.size());
                Mem::make_rx(it->target, it->backup.size());
            } else { 
                write_mem_proc(it->target, it->backup.data(), it->backup.size()); 
                Mem::flush_caches(it->target, it->backup.size()); 
            }
            g_ret_patches.erase(it); 
            return true;
        }
    }
    return false;
}

} // namespace sandhook