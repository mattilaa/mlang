    .section .bss
    .align 3
wq_waiter_id:
    .zero 256
wq_awake:
    .zero 256

    .text
    .global mlnix_wq_waiter_id_get
mlnix_wq_waiter_id_get:
    adrp x9, wq_waiter_id
    add x9, x9, :lo12:wq_waiter_id
    add x9, x9, x0, lsl #3
    ldr x0, [x9]
    ret

    .global mlnix_wq_waiter_id_set
mlnix_wq_waiter_id_set:
    adrp x9, wq_waiter_id
    add x9, x9, :lo12:wq_waiter_id
    add x9, x9, x0, lsl #3
    str x1, [x9]
    ret

    .global mlnix_wq_awake_get
mlnix_wq_awake_get:
    adrp x9, wq_awake
    add x9, x9, :lo12:wq_awake
    add x9, x9, x0, lsl #3
    ldr x0, [x9]
    ret

    .global mlnix_wq_awake_set
mlnix_wq_awake_set:
    adrp x9, wq_awake
    add x9, x9, :lo12:wq_awake
    add x9, x9, x0, lsl #3
    str x1, [x9]
    ret
