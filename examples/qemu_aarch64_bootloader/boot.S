.section .text.boot, "ax"
.global _start
.type _start, %function

_start:
    ldr x0, =__stack_top
    mov sp, x0
    bl kernel_main__void

1:
    wfe
    b 1b

.size _start, . - _start
