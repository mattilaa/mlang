.section .text.boot, "ax"
.global _start
.extern kernel_main__void

_start:
    ldr x0, =__stack_top
    mov sp, x0
    mrs x1, cpacr_el1
    orr x1, x1, #(3 << 20)
    msr cpacr_el1, x1
    isb
    bl kernel_main__void

1:
    wfe
    b 1b

.section .bss.stack, "aw", %nobits
.align 12
__stack_bottom:
    .space 16384
__stack_top:
