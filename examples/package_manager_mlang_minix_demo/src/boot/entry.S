.section .text.boot, "ax"
.global _start
.extern kernel_main__void
.extern runtime_handle_irq
.extern runtime_unhandled_exception
.extern runtime_syscall_dispatch
.extern runtime_user_fault

.macro vector_slot target
    b \target
    .space 124
.endm

_start:
    msr spsel, #1
    ldr x0, =__stack_top
    mov sp, x0
    msr daifset, #0xf
    ldr x0, =vectors
    msr vbar_el1, x0
    isb
    mrs x1, cpacr_el1
    orr x1, x1, #(3 << 20)
    msr cpacr_el1, x1
    ldr x0, =boot_l1_table
    msr ttbr0_el1, x0
    mov x0, xzr
    msr ttbr1_el1, x0
    ldr x0, =0x00000000000000ff
    msr mair_el1, x0
    ldr x0, =0x0000000200003519
    msr tcr_el1, x0
    dsb ish
    tlbi vmalle1
    dsb ish
    isb
    mrs x1, sctlr_el1
    bic x1, x1, #(1 << 1)
    bic x1, x1, #(1 << 3)
    bic x1, x1, #(1 << 4)
    bic x1, x1, #(1 << 19)
    orr x1, x1, #(1 << 0)
    orr x1, x1, #(1 << 2)
    orr x1, x1, #(1 << 12)
    msr sctlr_el1, x1
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
    vector_slot vector_sync_lower_a64
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

vector_sync_lower_a64:
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

    mrs x9, esr_el1
    lsr x9, x9, #26
    cmp x9, #0x15
    b.ne .Lsync_lower_fault

    mov x6, x5
    mov x5, x4
    mov x4, x3
    mov x3, x2
    mov x2, x1
    mov x1, x0
    mov x0, x8
    bl runtime_syscall_dispatch
    str x0, [sp, #0]
    b .Lsync_lower_return

.Lsync_lower_fault:
    mrs x0, esr_el1
    mrs x1, elr_el1
    mrs x2, far_el1
    bl runtime_user_fault

.Lsync_lower_return:
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

vector_unhandled:
    b vector_sync_current_spx

.global runtime_enter_user
runtime_enter_user:
    stp x29, x30, [sp, #-16]!
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    mov x29, sp

    adrp x9, __user_exit_ksp
    add  x9, x9, :lo12:__user_exit_ksp
    mov  x10, sp
    str  x10, [x9]

    msr elr_el1, x0
    msr sp_el0, x1
    mov x9, #0x3c0
    msr spsr_el1, x9

    mov x0, x2
    mov x1, x3
    mov x2, xzr
    mov x3, xzr
    mov x4, xzr
    mov x5, xzr
    mov x6, xzr
    mov x7, xzr
    mov x8, xzr

    dsb sy
    isb
    eret

.global runtime_exit_to_kernel
runtime_exit_to_kernel:
    adrp x9, __user_exit_ksp
    add  x9, x9, :lo12:__user_exit_ksp
    ldr  x10, [x9]
    mov  sp, x10
    ldp x27, x28, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ldp x29, x30, [sp], #16
    msr daifclr, #2
    ret

.section .data
.align 3
.global __user_exit_ksp
__user_exit_ksp:
    .quad 0

.align 12
boot_l1_table:
    .quad 0x0060000000000405
    .quad boot_l2_table + 0x3
    .fill 510, 8, 0

.align 12
boot_l2_table:
    .quad 0x0000000040000741
    .quad 0x0000000040200741
    .quad 0x0000000040400741
    .quad 0x0000000040600741
    .fill 108, 8, 0
    .quad 0x000000004e000781
    .quad 0x006000004e200741
    .quad 0x006000004e400741
    .quad 0x006000004e600741
    .quad 0x006000004e800741
    .quad 0x006000004ea00741
    .fill 394, 8, 0

.section .bss.stack, "aw", %nobits
.align 12
__stack_bottom:
    .space 16384
__stack_top:

.align 12
.global __user_stack_bottom
__user_stack_bottom:
    .space 262144
.global __user_stack_top
__user_stack_top:
