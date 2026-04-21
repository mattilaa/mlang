    .section .bss
    .align 3
rx_buffer:
    .zero 256
rx_head:
    .zero 8
rx_tail:
    .zero 8

    .text
    .global mlnix_rx_byte_get
mlnix_rx_byte_get:
    adrp x9, rx_buffer
    add x9, x9, :lo12:rx_buffer
    ldrb w0, [x9, x0]
    ret

    .global mlnix_rx_byte_set
mlnix_rx_byte_set:
    adrp x9, rx_buffer
    add x9, x9, :lo12:rx_buffer
    strb w1, [x9, x0]
    ret

    .global mlnix_rx_head_get
mlnix_rx_head_get:
    adrp x9, rx_head
    add x9, x9, :lo12:rx_head
    ldr x0, [x9]
    ret

    .global mlnix_rx_head_set
mlnix_rx_head_set:
    adrp x9, rx_head
    add x9, x9, :lo12:rx_head
    str x0, [x9]
    ret

    .global mlnix_rx_tail_get
mlnix_rx_tail_get:
    adrp x9, rx_tail
    add x9, x9, :lo12:rx_tail
    ldr x0, [x9]
    ret

    .global mlnix_rx_tail_set
mlnix_rx_tail_set:
    adrp x9, rx_tail
    add x9, x9, :lo12:rx_tail
    str x0, [x9]
    ret
