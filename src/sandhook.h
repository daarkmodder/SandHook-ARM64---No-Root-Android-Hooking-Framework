// sandhook.h
// Production-Grade ARM64 Hook Framework for Android (No-Root)
// Version: 4.8

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
#define HOOK_THREAD_SUSPENSION_FAILED 8
#define HOOK_OUT_OF_RANGE           9
#define HOOK_ERR_PAC                10 // Fallo de autenticación de puntero

int sandhook_install_ex(void* target, void* replacement, void** original_out);
void* sandhook_install(void* target, void* replacement, void** original_out);
int sandhook_install_single_insn(void* target, void* replacement, void** original_out);
int sandhook_remove(void* target);
void* sandhook_trampoline(void* target);

// Utility & Pending Hooks
const char* sandhook_version();
const char* sandhook_error_string(int err);
int sandhook_install_pending(const char* lib_name, const char* sym_name, void* replacement, void** original_out);

#ifdef __cplusplus
}
#endif