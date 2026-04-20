    .section .bss
    .align 12
mlnix_boot_stack:
    .skip 16384
mlnix_boot_stack_top:

    .section .text.boot
    .global _start
_start:
    ldr x0, =mlnix_boot_stack_top
    mov sp, x0
    bl boot_entry__void
1:
    b 1b
