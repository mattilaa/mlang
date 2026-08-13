.section .text, "ax"

// Write a zero-terminated MLang str8 literal to QEMU virt's PL011 UART.
.global serial_write
.type serial_write, %function
serial_write:
    ldr x1, =0x09000000
1:
    ldrb w2, [x0], #1
    cbz w2, 3f
2:
    ldr w3, [x1, #0x18]
    tbnz w3, #5, 2b
    str w2, [x1]
    b 1b
3:
    ret
.size serial_write, . - serial_write

// MLang currently emits hosted exception-frame calls for every function.
// This narrow freestanding shim keeps the normal path usable without libc.
.global __mlang_std_exceptions_push_frame
__mlang_std_exceptions_push_frame:
    mov x0, #0
    ret

.global __mlang_std_exceptions_frame_env
__mlang_std_exceptions_frame_env:
    adrp x0, __mlang_boot_jmp_buffer
    add x0, x0, :lo12:__mlang_boot_jmp_buffer
    ret

.global _setjmp
_setjmp:
    mov w0, #0
    ret

.global __mlang_std_exceptions_pop_frame
__mlang_std_exceptions_pop_frame:
    ret

.global __mlang_std_exceptions_rethrow_current
__mlang_std_exceptions_rethrow_current:
1:
    wfe
    b 1b

.section .bss, "aw", %nobits
.balign 16
__mlang_boot_jmp_buffer:
    .skip 256
