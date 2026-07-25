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

static bool read_proc_maps_syscall(std::string& out) {
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
    if (page_protect(addr, len, PROT_READ | PROT_EXEC)) return true;
    return page_protect(addr, len, PROT_READ | PROT_EXEC | PROT_BTI);
}
void Mem::flush_caches(void* addr, std::size_t len) {
    __builtin___clear_cache(reinterpret_cast<char*>(addr), reinterpret_cast<char*>(addr) + len);
    asm volatile("isb" ::: "memory"); asm volatile("dsb sy" ::: "memory");
}

void atomic_write_inst(void* target, const void* inst, size_t len) {
    std::memcpy(target, inst, len); // Safe with StopTheWorld
}

} // namespace sandhook