#include "../internal/sandhook_internal.h"
#include <sstream>
#include <map>

namespace sandhook {

// ============================================================================
// xDL CACHE: Evita abrir la misma librería múltiples veces
// ============================================================================
static std::map<std::string, void*> g_xdl_cache;
static std::mutex g_xdl_cache_mu;

void* get_cached_xdl_handle(const char* lib_name) {
    if (!lib_name) return nullptr;
    std::lock_guard<std::mutex> lk(g_xdl_cache_mu);
    auto it = g_xdl_cache.find(lib_name);
    if (it != g_xdl_cache.end()) return it->second;
    
    void* handle = xdl_open(lib_name, XDL_DEFAULT);
    if (handle) g_xdl_cache[lib_name] = handle;
    return handle;
}

// ============================================================================
// LIB LENGTH & PATTERN SCANNER
// ============================================================================
static bool get_lib_base_and_size(const char* lib_name, uintptr_t* base, size_t* size) {
    std::string maps;
    if (!read_proc_maps_syscall(maps)) return false; // De mem.cpp
    
    std::istringstream stream(maps);
    std::string line;
    *base = 0; *size = 0; uintptr_t end = 0;
    bool found = false;
    
    while (std::getline(stream, line)) {
        if (line.find(lib_name) == std::string::npos) continue;
        if (line.find(" r-x ") == std::string::npos && line.find(" r-xp") == std::string::npos) continue;
        
        uintptr_t start_addr = 0, end_addr = 0;
        if (sscanf(line.c_str(), "%lx-%lx", &start_addr, &end_addr) == 2) {
            if (!found) { *base = start_addr; found = true; }
            end = end_addr;
        }
    }
    if (found && end > *base) { *size = end - *base; return true; }
    return false;
}

uintptr_t find_pattern_in_lib(const char* lib_name, const char* pattern) {
    uintptr_t base = 0; size_t size = 0;
    if (!get_lib_base_and_size(lib_name, &base, &size) || size == 0) return 0;

    // Parsear el patrón (ej: "AA BB ?? CC")
    std::vector<uint8_t> bytes;
    std::vector<bool> wildcard;
    std::string p = pattern;
    p.erase(std::remove(p.begin(), p.end(), ' '), p.end());
    
    for (size_t i = 0; i < p.length(); i += 2) {
        std::string hex = p.substr(i, 2);
        if (hex == "??") { bytes.push_back(0); wildcard.push_back(true); }
        else { bytes.push_back((uint8_t)strtol(hex.c_str(), nullptr, 16)); wildcard.push_back(false); }
    }

    if (bytes.empty()) return 0;

    // Escanear memoria
    uint8_t* start = (uint8_t*)base;
    size_t len = bytes.size();
    
    SigGuard guard(base, base + size); // Proteger contra SIGSEGV
    if (guard.jumped()) return 0;

    for (size_t i = 0; i <= size - len; i++) {
        bool found = true;
        for (size_t j = 0; j < len; j++) {
            if (!wildcard[j] && start[i + j] != bytes[j]) { found = false; break; }
        }
        if (found) return (uintptr_t)(start + i);
    }
    return 0;
}

// ============================================================================
// LOW LEVEL WRITE NOP / RET
// ============================================================================
bool write_nop_internal(void* addr, int count) {
    if (!addr || count <= 0) return false;
    size_t len = count * 4;
    if (!Mem::make_rw(addr, len)) return false;
    
    uint32_t nop = 0xD503201F;
    for (int i = 0; i < count; i++) std::memcpy((uint8_t*)addr + i * 4, &nop, 4);
    
    Mem::flush_caches(addr, len);
    Mem::make_rx(addr, len);
    return true;
}

bool write_ret_internal(void* addr) {
    if (!addr) return false;
    if (!Mem::make_rw(addr, 4)) return false;
    
    uint32_t ret = 0xD65F03C0;
    std::memcpy(addr, &ret, 4);
    
    Mem::flush_caches(addr, 4);
    Mem::make_rx(addr, 4);
    return true;
}

} // namespace sandhook

// --- C API Bridge ---
extern "C" {
uintptr_t sandhook_find_pattern(const char* lib_name, const char* pattern) { return sandhook::find_pattern_in_lib(lib_name, pattern); }
void* sandhook_get_lib_handle(const char* lib_name) { return sandhook::get_cached_xdl_handle(lib_name); }
bool sandhook_write_nop(void* addr, int count) { return sandhook::write_nop_internal(addr, count); }
bool sandhook_write_ret(void* addr) { return sandhook::write_ret_internal(addr); }
}