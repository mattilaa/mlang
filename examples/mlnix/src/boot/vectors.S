    .section .text.vectors
    .align 11
vectors_base:
    b vector_sync_sp0
    .space 0x7c
    b vector_irq_sp0
    .space 0x7c
    b vector_fiq_sp0
    .space 0x7c
    b vector_serr_sp0
    .space 0x7c
    b vector_sync_spx
    .space 0x7c
    b vector_irq_spx
    .space 0x7c
    b vector_fiq_spx
    .space 0x7c
    b vector_serr_spx
    .space 0x7c
    b vector_sync_lower_a64
    .space 0x7c
    b vector_irq_lower_a64
    .space 0x7c
    b vector_fiq_lower_a64
    .space 0x7c
    b vector_serr_lower_a64
    .space 0x7c
    b vector_sync_lower_a32
    .space 0x7c
    b vector_irq_lower_a32
    .space 0x7c
    b vector_fiq_lower_a32
    .space 0x7c
    b vector_serr_lower_a32
    .space 0x7c

    .macro vector_stub name, id
    .align 7
\name:
    mov x0, #\id
    bl exception_handle__i64
1:
    wfe
    b 1b
    .endm

    .macro irq_stub name, id
    .align 7
\name:
    sub sp, sp, #256
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    mov x0, #\id
    bl irq_handle__i64
    ldr x30, [sp, #240]
    ldp x28, x29, [sp, #224]
    ldp x26, x27, [sp, #208]
    ldp x24, x25, [sp, #192]
    ldp x22, x23, [sp, #176]
    ldp x20, x21, [sp, #160]
    ldp x18, x19, [sp, #144]
    ldp x16, x17, [sp, #128]
    ldp x14, x15, [sp, #112]
    ldp x12, x13, [sp, #96]
    ldp x10, x11, [sp, #80]
    ldp x8, x9, [sp, #64]
    ldp x6, x7, [sp, #48]
    ldp x4, x5, [sp, #32]
    ldp x2, x3, [sp, #16]
    ldp x0, x1, [sp, #0]
    add sp, sp, #256
    eret
    .endm

    vector_stub vector_sync_sp0, 0
    irq_stub vector_irq_sp0, 1
    vector_stub vector_fiq_sp0, 2
    vector_stub vector_serr_sp0, 3
    vector_stub vector_sync_spx, 4
    irq_stub vector_irq_spx, 5
    vector_stub vector_fiq_spx, 6
    vector_stub vector_serr_spx, 7
    vector_stub vector_sync_lower_a64, 8
    irq_stub vector_irq_lower_a64, 9
    vector_stub vector_fiq_lower_a64, 10
    vector_stub vector_serr_lower_a64, 11
    vector_stub vector_sync_lower_a32, 12
    irq_stub vector_irq_lower_a32, 13
    vector_stub vector_fiq_lower_a32, 14
    vector_stub vector_serr_lower_a32, 15

    .text
    .global vectors_install
vectors_install:
    ldr x0, =vectors_base
    msr VBAR_EL1, x0
    isb
    ret
