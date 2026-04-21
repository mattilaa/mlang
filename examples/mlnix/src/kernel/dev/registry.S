    .section .bss
    .align 3
dev_kind:
    .zero 64
dev_base:
    .zero 64
dev_irq:
    .zero 64
dev_flags:
    .zero 64
dev_capacity:
    .zero 64
dev_count:
    .zero 8

    .text
    .global mlnix_dev_kind_get
mlnix_dev_kind_get:
    adrp x9, dev_kind
    add x9, x9, :lo12:dev_kind
    add x9, x9, x0, lsl #3
    ldr x0, [x9]
    ret

    .global mlnix_dev_kind_set
mlnix_dev_kind_set:
    adrp x9, dev_kind
    add x9, x9, :lo12:dev_kind
    add x9, x9, x0, lsl #3
    str x1, [x9]
    ret

    .global mlnix_dev_base_get
mlnix_dev_base_get:
    adrp x9, dev_base
    add x9, x9, :lo12:dev_base
    add x9, x9, x0, lsl #3
    ldr x0, [x9]
    ret

    .global mlnix_dev_base_set
mlnix_dev_base_set:
    adrp x9, dev_base
    add x9, x9, :lo12:dev_base
    add x9, x9, x0, lsl #3
    str x1, [x9]
    ret

    .global mlnix_dev_irq_get
mlnix_dev_irq_get:
    adrp x9, dev_irq
    add x9, x9, :lo12:dev_irq
    add x9, x9, x0, lsl #3
    ldr x0, [x9]
    ret

    .global mlnix_dev_irq_set
mlnix_dev_irq_set:
    adrp x9, dev_irq
    add x9, x9, :lo12:dev_irq
    add x9, x9, x0, lsl #3
    str x1, [x9]
    ret

    .global mlnix_dev_flags_get
mlnix_dev_flags_get:
    adrp x9, dev_flags
    add x9, x9, :lo12:dev_flags
    add x9, x9, x0, lsl #3
    ldr x0, [x9]
    ret

    .global mlnix_dev_flags_set
mlnix_dev_flags_set:
    adrp x9, dev_flags
    add x9, x9, :lo12:dev_flags
    add x9, x9, x0, lsl #3
    str x1, [x9]
    ret

    .global mlnix_dev_capacity_get
mlnix_dev_capacity_get:
    adrp x9, dev_capacity
    add x9, x9, :lo12:dev_capacity
    add x9, x9, x0, lsl #3
    ldr x0, [x9]
    ret

    .global mlnix_dev_capacity_set
mlnix_dev_capacity_set:
    adrp x9, dev_capacity
    add x9, x9, :lo12:dev_capacity
    add x9, x9, x0, lsl #3
    str x1, [x9]
    ret

    .global mlnix_dev_count_get
mlnix_dev_count_get:
    adrp x9, dev_count
    add x9, x9, :lo12:dev_count
    ldr x0, [x9]
    ret

    .global mlnix_dev_count_set
mlnix_dev_count_set:
    adrp x9, dev_count
    add x9, x9, :lo12:dev_count
    str x0, [x9]
    ret
