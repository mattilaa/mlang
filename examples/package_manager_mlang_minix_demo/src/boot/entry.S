.section .text.boot, "ax"
.global _start
.extern kernel_main__void
.extern runtime_handle_irq
.extern runtime_unhandled_exception

.macro vector_slot target
    b \target
    .space 124
.endm

_start:
    ldr x0, =__stack_top
    mov sp, x0
    msr daifset, #0xf
    ldr x0, =vectors
    msr vbar_el1, x0
    isb
    mrs x1, cpacr_el1
    orr x1, x1, #(3 << 20)
    msr cpacr_el1, x1
    isb
    bl kernel_main__void

1:
    wfe
    b 1b

.align 11
vectors:
    vector_slot vector_unhandled
    vector_slot vector_unhandled
    vector_slot vector_unhandled
    vector_slot vector_unhandled
    vector_slot vector_sync_current_spx
    vector_slot vector_irq_current_spx
    vector_slot vector_unhandled
    vector_slot vector_unhandled
    vector_slot vector_unhandled
    vector_slot vector_irq_lower_a64
    vector_slot vector_unhandled
    vector_slot vector_unhandled
    vector_slot vector_unhandled
    vector_slot vector_unhandled
    vector_slot vector_unhandled
    vector_slot vector_unhandled

vector_sync_current_spx:
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
    bl runtime_unhandled_exception
    b .

vector_irq_current_spx:
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
    bl runtime_handle_irq
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

vector_irq_lower_a64:
    b vector_irq_current_spx

vector_unhandled:
    b vector_sync_current_spx

.section .bss.stack, "aw", %nobits
.align 12
__stack_bottom:
    .space 16384
__stack_top:
