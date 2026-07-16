// sandhook.h
// Public C API for SandHook ARM64 Native Engine
// This header provides the interface to install, remove, and manage hooks
// at the native memory level for Android ARM64.
// Compatible with C and C++.

#pragma once

#include <stdint.h>
#include <stddef.h>

// Ensure C linkage so it can be used from C, C++, and JNI without name mangling
#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// ERROR CODES
// ============================================================================
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

// ============================================================================
// API FUNCTIONS
// ============================================================================

/**
 * Installs a standard inline hook (20 bytes patch).
 * This method is safe and supports creating a trampoline to call the original function.
 *
 * @param target       Pointer to the function to be hooked.
 * @param replacement  Pointer to the replacement function.
 * @param original_out Pointer to store the trampoline (call original). Can be NULL.
 * @return HOOK_OK on success, or an error code on failure.
 */
int sandhook_install_ex(void* target, void* replacement, void** original_out);

/**
 * Simplified version of sandhook_install_ex.
 * 
 * @param target       Pointer to the function to be hooked.
 * @param replacement  Pointer to the replacement function.
 * @param original_out Pointer to store the trampoline. Can be NULL.
 * @return The target pointer on success, or NULL on failure.
 */
void* sandhook_install(void* target, void* replacement, void** original_out);

/**
 * Installs a single instruction hook (4 bytes patch).
 * Attempts a fast, atomic relative branch (B). 
 * Note: If a backup/trampoline is requested (original_out != NULL) or the target 
 * is out of the 128MB relative range, it automatically falls back to the 20-byte standard hook.
 *
 * @param target       Pointer to the function to be hooked.
 * @param replacement  Pointer to the replacement function.
 * @param original_out Pointer to store the trampoline. (Recommended NULL for pure single insn).
 * @return HOOK_OK on success, or an error code on failure.
 */
int sandhook_install_single_insn(void* target, void* replacement, void** original_out);

/**
 * Removes an installed hook and restores the original instructions.
 *
 * @param target Pointer to the function that was hooked.
 * @return HOOK_OK on success, or an error code on failure.
 */
int sandhook_remove(void* target);

/**
 * Retrieves the trampoline pointer for a hooked function.
 *
 * @param target Pointer to the hooked function.
 * @return Pointer to the trampoline, or NULL if not hooked.
 */
void* sandhook_trampoline(void* target);

/**
 * Gets the current version of the SandHook native engine.
 *
 * @return Constant string representing the version.
 */
const char* sandhook_version();

/**
 * Converts an error code into a human-readable string.
 *
 * @param err The error code returned by install/remove functions.
 * @return Constant string describing the error.
 */
const char* sandhook_error_string(int err);

#ifdef __cplusplus
} // extern "C"
#endif