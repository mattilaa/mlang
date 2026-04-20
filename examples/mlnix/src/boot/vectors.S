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

    vector_stub vector_sync_sp0, 0
    vector_stub vector_irq_sp0, 1
    vector_stub vector_fiq_sp0, 2
    vector_stub vector_serr_sp0, 3
    vector_stub vector_sync_spx, 4
    vector_stub vector_irq_spx, 5
    vector_stub vector_fiq_spx, 6
    vector_stub vector_serr_spx, 7
    vector_stub vector_sync_lower_a64, 8
    vector_stub vector_irq_lower_a64, 9
    vector_stub vector_fiq_lower_a64, 10
    vector_stub vector_serr_lower_a64, 11
    vector_stub vector_sync_lower_a32, 12
    vector_stub vector_irq_lower_a32, 13
    vector_stub vector_fiq_lower_a32, 14
    vector_stub vector_serr_lower_a32, 15

    .text
    .global vectors_install
vectors_install:
    ldr x0, =vectors_base
    msr VBAR_EL1, x0
    isb
    ret
