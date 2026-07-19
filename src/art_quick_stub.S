// art_quick_stub.S
// Trampolín Ensamblador ARM64 (Inspirado en Dobby Closure Bridge)
// Salva el contexto COMPLETO de la CPU para ART.

.text
.align 2
.global sandhook_art_quick_stub
.type sandhook_art_quick_stub, %function

sandhook_art_quick_stub:
    // 1. Salvar registros de coma flotante (q0-q7)
    sub sp, sp, #(8 * 16)
    stp q6, q7, [sp, #(6 * 16)]
    stp q4, q5, [sp, #(4 * 16)]
    stp q2, q3, [sp, #(2 * 16)]
    stp q0, q1, [sp, #(0 * 16)]

    // 2. Salvar registros enteros (x1-x30)
    sub sp, sp, #(30 * 8)
    stp x29, x30, [sp, #(28 * 8)]
    stp x27, x28, [sp, #(26 * 8)]
    stp x25, x26, [sp, #(24 * 8)]
    stp x23, x24, [sp, #(22 * 8)]
    stp x21, x22, [sp, #(20 * 8)]
    stp x19, x20, [sp, #(18 * 8)]
    stp x17, x18, [sp, #(16 * 8)]
    stp x15, x16, [sp, #(14 * 8)]
    stp x13, x14, [sp, #(12 * 8)]
    stp x11, x12, [sp, #(10 * 8)]
    stp x9, x10, [sp, #(8 * 8)]
    stp x7, x8, [sp, #(6 * 8)]
    stp x5, x6, [sp, #(4 * 8)]
    stp x3, x4, [sp, #(2 * 8)]
    stp x1, x2, [sp, #(0 * 8)]

    // 3. Salvar x0 (ArtMethod*) y calcular el SP original
    sub sp, sp, #(2 * 8)
    str x0, [sp, #(1 * 8)] // Guardar x0

    // Calcular el SP original (SP antes de que empezáramos a restar)
    add x17, sp, #(2 * 8)               // x17 = sp + 16
    add x17, x17, #(30 * 8 + 8 * 16)    // x17 = sp + 16 + 240 + 128 (SP original)
    str x17, [sp, #(0 * 8)]             // Guardar SP original en la pila

    // 4. Llamar al Handler de C++
    // x0 = Puntero al contexto (sp actual)
    // x1 = ArtMethod* (lo sacamos de donde lo guardamos)
    mov x0, sp
    ldr x1, [sp, #(1 * 8)]
    bl sandhook_art_quick_handler

    // 5. Restaurar x0
    ldr x0, [sp, #(1 * 8)]
    add sp, sp, #(2 * 8)

    // 6. Restaurar registros enteros (x1-x30)
    ldp x1, x2, [sp, #(0 * 8)]
    ldp x3, x4, [sp, #(2 * 8)]
    ldp x5, x6, [sp, #(4 * 8)]
    ldp x7, x8, [sp, #(6 * 8)]
    ldp x9, x10, [sp, #(8 * 8)]
    ldp x11, x12, [sp, #(10 * 8)]
    ldp x13, x14, [sp, #(12 * 8)]
    ldp x15, x16, [sp, #(14 * 8)]
    ldp x17, x18, [sp, #(16 * 8)]
    ldp x19, x20, [sp, #(18 * 8)]
    ldp x21, x22, [sp, #(20 * 8)]
    ldp x23, x24, [sp, #(22 * 8)]
    ldp x25, x26, [sp, #(24 * 8)]
    ldp x27, x28, [sp, #(26 * 8)]
    ldp x29, x30, [sp, #(28 * 8)]
    add sp, sp, #(30 * 8)

    // 7. Restaurar registros de coma flotante (q0-q7)
    ldp q0, q1, [sp, #(0 * 16)]
    ldp q2, q3, [sp, #(2 * 16)]
    ldp q4, q5, [sp, #(4 * 16)]
    ldp q6, q7, [sp, #(6 * 16)]
    add sp, sp, #(8 * 16)

    // 8. Saltar a la función original
    // La dirección original fue guardada por el handler de C++ en x8
    br x8