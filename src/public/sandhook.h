#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOOK_OK                     0
#define HOOK_NULL_ARGS              1
#define HOOK_ALREADY_HOOKED         2
#define HOOK_RELOCATION_FAILED      3
#define HOOK_MPROTECT_FAILED        4
#define HOOK_ALLOC_FAILED           5
#define HOOK_INVALID_TARGET         6
#define HOOK_BOUNDS_EXCEEDED        7
#define HOOK_OUT_OF_RANGE           9
#define HOOK_ERR_PAC                10
#define HOOK_PENDING                11
#define HOOK_PROTECTED              12

// --- API Principal ---
int sandhook_install_ex(void* target, void* replacement, void** original_out);
int sandhook_remove(void* target);
int sandhook_ret_patch(void* target, int64_t return_value, int use_return_value);
int sandhook_install_pending(const char* lib_name, const char* sym_name, void* replacement, void** original_out);

// --- Nuevas Utilidades (ARMPatch Style) ---
uintptr_t sandhook_find_pattern(const char* lib_name, const char* pattern);
void* sandhook_get_lib_handle(const char* lib_name);
bool sandhook_write_nop(void* addr, int count);
bool sandhook_write_ret(void* addr);

// --- Macros de Conveniencia ---
#define DECL_HOOK(ret, name, ...) static ret (*orig_##name)(__VA_ARGS__) = nullptr; ret name(__VA_ARGS__)
#define HOOK_ADDR(name, addr) sandhook_install_ex((void*)(addr), (void*)(name), (void**)&(orig_##name))
#define HOOK_SYM(name, lib, sym) sandhook_install_pending(lib, sym, (void*)(name), (void**)&(orig_##name))

const char* sandhook_version();
const char* sandhook_error_string(int err);

#ifdef __cplusplus
}
#endif