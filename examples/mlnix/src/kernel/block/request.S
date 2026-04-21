    .section .bss
    .align 3
request_dev_id:
    .zero 32
request_op:
    .zero 32
request_lba:
    .zero 32
request_status:
    .zero 32
request_wq_slot:
    .zero 32
request_buffers:
    .zero 2048

    .text
    .global mlnix_request_dev_id_get
mlnix_request_dev_id_get:
    adrp x9, request_dev_id
    add x9, x9, :lo12:request_dev_id
    add x9, x9, x0, lsl #3
    ldr x0, [x9]
    ret

    .global mlnix_request_dev_id_set
mlnix_request_dev_id_set:
    adrp x9, request_dev_id
    add x9, x9, :lo12:request_dev_id
    add x9, x9, x0, lsl #3
    str x1, [x9]
    ret

    .global mlnix_request_op_get
mlnix_request_op_get:
    adrp x9, request_op
    add x9, x9, :lo12:request_op
    add x9, x9, x0, lsl #3
    ldr x0, [x9]
    ret

    .global mlnix_request_op_set
mlnix_request_op_set:
    adrp x9, request_op
    add x9, x9, :lo12:request_op
    add x9, x9, x0, lsl #3
    str x1, [x9]
    ret

    .global mlnix_request_lba_get
mlnix_request_lba_get:
    adrp x9, request_lba
    add x9, x9, :lo12:request_lba
    add x9, x9, x0, lsl #3
    ldr x0, [x9]
    ret

    .global mlnix_request_lba_set
mlnix_request_lba_set:
    adrp x9, request_lba
    add x9, x9, :lo12:request_lba
    add x9, x9, x0, lsl #3
    str x1, [x9]
    ret

    .global mlnix_request_status_get
mlnix_request_status_get:
    adrp x9, request_status
    add x9, x9, :lo12:request_status
    add x9, x9, x0, lsl #3
    ldr x0, [x9]
    ret

    .global mlnix_request_status_set
mlnix_request_status_set:
    adrp x9, request_status
    add x9, x9, :lo12:request_status
    add x9, x9, x0, lsl #3
    str x1, [x9]
    ret

    .global mlnix_request_wq_slot_get
mlnix_request_wq_slot_get:
    adrp x9, request_wq_slot
    add x9, x9, :lo12:request_wq_slot
    add x9, x9, x0, lsl #3
    ldr x0, [x9]
    ret

    .global mlnix_request_wq_slot_set
mlnix_request_wq_slot_set:
    adrp x9, request_wq_slot
    add x9, x9, :lo12:request_wq_slot
    add x9, x9, x0, lsl #3
    str x1, [x9]
    ret

    // x0 = slot; returns byte address of slot's 512B buffer
    .global mlnix_request_buf_base
mlnix_request_buf_base:
    adrp x9, request_buffers
    add x9, x9, :lo12:request_buffers
    mov x10, #512
    mul x10, x0, x10
    add x0, x9, x10
    ret

    // x0 = slot, x1 = offset; returns byte at buffer[slot][offset]
    .global mlnix_request_buf_byte
mlnix_request_buf_byte:
    adrp x9, request_buffers
    add x9, x9, :lo12:request_buffers
    mov x10, #512
    mul x10, x0, x10
    add x9, x9, x10
    ldrb w0, [x9, x1]
    ret

    // x0 = slot, x1 = fill byte; fills slot buffer with 512 copies of byte
    .global mlnix_request_buf_fill
mlnix_request_buf_fill:
    adrp x9, request_buffers
    add x9, x9, :lo12:request_buffers
    mov x10, #512
    mul x10, x0, x10
    add x9, x9, x10
    mov x11, #0
1:
    cmp x11, #512
    b.ge 2f
    strb w1, [x9, x11]
    add x11, x11, #1
    b 1b
2:
    ret
