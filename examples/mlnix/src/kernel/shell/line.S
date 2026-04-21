    .section .bss
    .align 3
line_buffer:
    .zero 64
line_length:
    .zero 8
line_pending:
    .zero 8

    .text
    .global mlnix_line_byte_get
mlnix_line_byte_get:
    adrp x9, line_buffer
    add x9, x9, :lo12:line_buffer
    ldrb w0, [x9, x0]
    ret

    .global mlnix_line_byte_set
mlnix_line_byte_set:
    adrp x9, line_buffer
    add x9, x9, :lo12:line_buffer
    strb w1, [x9, x0]
    ret

    .global mlnix_line_length_get
mlnix_line_length_get:
    adrp x9, line_length
    add x9, x9, :lo12:line_length
    ldr x0, [x9]
    ret

    .global mlnix_line_length_set
mlnix_line_length_set:
    adrp x9, line_length
    add x9, x9, :lo12:line_length
    str x0, [x9]
    ret

    .global mlnix_line_pending_get
mlnix_line_pending_get:
    adrp x9, line_pending
    add x9, x9, :lo12:line_pending
    ldr x0, [x9]
    ret

    .global mlnix_line_pending_set
mlnix_line_pending_set:
    adrp x9, line_pending
    add x9, x9, :lo12:line_pending
    str x0, [x9]
    ret
