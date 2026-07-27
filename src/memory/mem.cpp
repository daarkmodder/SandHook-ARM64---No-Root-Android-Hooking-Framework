#include "../internal/sandhook_internal.h"
#include <unistd.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <sstream>

namespace sandhook {

static inline Address align_down(Address v, std::size_t a = kPageSize) { return v & ~(static_cast<Address>(a - 1)); }
static inline Address align_up(Address v, std::size_t a = kPageSize) {
    Address aligned_down = v & ~(static_cast<Address>(a - 1));
    return (v == aligned_down) ? v : (aligned_down + a);
}

bool write_mem_proc(void* addr, const void* data, size_t len) {
    const char* mem_path = AY_OBFUSCATE("/proc/self/mem");
    int fd = syscall(SYS_openat, AT_FDCWD, mem_path, O_WRONLY | O_CLOEXEC, 0);
    if (fd >= 0) {
        if (syscall(SYS_lseek, fd, (off_t)addr, SEEK_SET) == (off_t)addr) {
            if (syscall(SYS_write, fd, data, len) == (ssize_t)len) {
                syscall(SYS_close, fd);
                return true;
            }
        }
        syscall(SYS_close, fd);
    }
    struct iovec local_iov; local_iov.iov_base = (void*)data; local_iov.iov_len = len;
    struct iovec remote_iov; remote_iov.iov_base = addr; remote_iov.iov_len = len;
    if (syscall(SYS_process_vm_writev, getpid(), &local_iov, 1, &remote_iov, 1, 0) == (ssize_t)len) return true;
    return false;
}

bool read_proc_maps_syscall(std::string& out) {
    const char* maps_path = AY_OBFUSCATE("/proc/self/maps");
    int fd = syscall(SYS_openat, AT_FDCWD, maps_path, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return false;
    char buf[4096]; ssize_t n;
    while ((n = syscall(SYS_read, fd, buf, sizeof(buf))) > 0) out.append(buf, n);
    syscall(SYS_close, fd);
    return n >= 0;
}

bool is_system_critical_lib(void* target) {
    std::string maps; if (!read_proc_maps_syscall(maps)) return false;
    uintptr_t addr = (uintptr_t)target;
    std::istringstream stream(maps); std::string line;
    const char* libdl_str = AY_OBFUSCATE("libdl.so");
    const char* linker_str = AY_OBFUSCATE("linker64");
    while (std::getline(stream, line)) {
        uintptr_t start, end;
        if (sscanf(line.c_str(), "%lx-%lx", &start, &end) == 2) {
            if (addr >= start && addr < end) {
                if (line.find(libdl_str) != std::string::npos || line.find(linker_str) != std::string::npos) return true;
                break;
            }
        }
    }
    return false;
}

bool is_executable_region(void* target) {
    std::string maps; if (!read_proc_maps_syscall(maps)) return false;
    uintptr_t addr = (uintptr_t)target;
    std::istringstream stream(maps); std::string line;
    const char* rwx1 = AY_OBFUSCATE(" r-x ");
    const char* rwx2 = AY_OBFUSCATE(" r-xp");
    while (std::getline(stream, line)) {
        uintptr_t start, end;
        if (sscanf(line.c_str(), "%lx-%lx", &start, &end) == 2) {
            if (addr >= start && addr < end) {
                if (line.find(rwx1) != std::string::npos || line.find(rwx2) != std::string::npos) return true;
                break;
            }
        }
    }
    return false;
}

bool Mem::page_protect(void* addr, std::size_t len, int prot) {
    if (!addr || !len) return false;
    Address start = align_down(reinterpret_cast<Address>(addr));
    Address end = align_up(reinterpret_cast<Address>(addr) + len);
    return syscall(SYS_mprotect, reinterpret_cast<void*>(start), end - start, prot) == 0;
}

bool Mem::make_rw(void* addr, std::size_t len) { return page_protect(addr, len, PROT_READ | PROT_WRITE); }

bool Mem::make_rx(void* addr, std::size_t len) {
    // Intento 1: mprotect normal
    if (page_protect(addr, len, PROT_READ | PROT_EXEC)) return true;
    if (page_protect(addr, len, PROT_READ | PROT_EXEC | PROT_BTI)) return true;

    // Intento 2: Dobby Page Shadowing (Bypass de SELinux execmod)
    Address start = align_down(reinterpret_cast<Address>(addr));
    Address end = align_up(reinterpret_cast<Address>(addr) + len);
    std::size_t page_len = end - start;
    
    // 1. Hacemos una copia de los bytes actuales (que ya tienen nuestro parche/RET)
    std::vector<std::uint8_t> backup(page_len);
    std::memcpy(backup.data(), reinterpret_cast<void*>(start), page_len);
    
    // 2. Mapeamos una nueva página anónima EXACTAMENTE en la misma dirección física
    void* new_mem = (void*)syscall(SYS_mmap, reinterpret_cast<void*>(start), page_len, 
                                   PROT_READ | PROT_WRITE, 
                                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (new_mem == MAP_FAILED) return false;
    
    // 3. Pegamos los bytes originales (con nuestro hook incluido) en la nueva página
    std::memcpy(new_mem, backup.data(), page_len);
    
    // 4. Ahora sí, le damos permisos de ejecución. Como es anónima, SELinux lo permite.
    if (page_protect(new_mem, page_len, PROT_READ | PROT_EXEC)) return true;
    return page_protect(new_mem, page_len, PROT_READ | PROT_EXEC | PROT_BTI);
}

void Mem::flush_caches(void* addr, std::size_t len) {
    __builtin___clear_cache(reinterpret_cast<char*>(addr), reinterpret_cast<char*>(addr) + len);
    asm volatile("isb" ::: "memory"); asm volatile("dsb sy" ::: "memory");
}

// ============================================================================
// FIX ShadowHook: Escritura atómica segura para evitar race conditions.
// ============================================================================
void atomic_write_inst(void* target, const void* inst, size_t len) {
    uintptr_t addr = reinterpret_cast<uintptr_t>(target);
    
    if (len == 4 && (addr % 4) == 0) {
        uint32_t val;
        std::memcpy(&val, inst, sizeof(val));
        __atomic_store_n(reinterpret_cast<uint32_t*>(addr), val, __ATOMIC_SEQ_CST);
    } 
    else if (len == 4 && (addr % 2) == 0) {
        // Escritura atómica en dos pasos de 2 bytes (para Thumb o desalineado)
        uint16_t* dst = reinterpret_cast<uint16_t*>(target);
        const uint16_t* src = reinterpret_cast<const uint16_t*>(inst);
        __atomic_store_n(&dst[0], src[0], __ATOMIC_RELAXED);
        __atomic_store_n(&dst[1], src[1], __ATOMIC_SEQ_CST);
    } 
    else if (len == 8 && (addr % 8) == 0) {
        uint64_t val;
        std::memcpy(&val, inst, sizeof(val));
        __atomic_store_n(reinterpret_cast<uint64_t*>(addr), val, __ATOMIC_SEQ_CST);
    } 
    else if (len == 8 && (addr % 4) == 0) {
        // Escritura atómica en dos pasos de 4 bytes
        uint32_t* dst = reinterpret_cast<uint32_t*>(target);
        const uint32_t* src = reinterpret_cast<const uint32_t*>(inst);
        __atomic_store_n(&dst[0], src[0], __ATOMIC_RELAXED);
        __atomic_store_n(&dst[1], src[1], __ATOMIC_SEQ_CST);
    } 
    else {
        // Fallback para 16 bytes (LDR + BR) o desalineamientos mayores.
        // Usamos memcpy porque el trampolín de 16 bytes tiene un NOP al final,
        // evitando que la CPU lea basura a medias.
        std::memcpy(target, inst, len);
    }
}

} // namespace sandhook