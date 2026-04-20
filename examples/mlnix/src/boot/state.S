    .section .bss
    .align 3
state_slots:
    .zero 64

    .text
    .global mlnix_state_get
mlnix_state_get:
    adrp x9, state_slots
    add x9, x9, :lo12:state_slots
    add x9, x9, x0, lsl #3
    ldr x0, [x9]
    ret

    .global mlnix_state_set
mlnix_state_set:
    adrp x9, state_slots
    add x9, x9, :lo12:state_slots
    add x9, x9, x0, lsl #3
    str x1, [x9]
    ret
