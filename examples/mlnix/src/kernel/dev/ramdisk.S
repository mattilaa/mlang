    .section .bss
    .align 3
ramdisk_storage:
    .zero 65536

    .text
    .global mlnix_ramdisk_base
mlnix_ramdisk_base:
    adrp x0, ramdisk_storage
    add x0, x0, :lo12:ramdisk_storage
    ret

    .global mlnix_ramdisk_read_byte
mlnix_ramdisk_read_byte:
    adrp x9, ramdisk_storage
    add x9, x9, :lo12:ramdisk_storage
    ldrb w0, [x9, x0]
    ret

    .global mlnix_ramdisk_write_byte
mlnix_ramdisk_write_byte:
    adrp x9, ramdisk_storage
    add x9, x9, :lo12:ramdisk_storage
    strb w1, [x9, x0]
    ret

    // x0 = byte_offset, x1 = dst ptr, x2 = nbytes
    .global mlnix_ramdisk_copy_out
mlnix_ramdisk_copy_out:
    adrp x9, ramdisk_storage
    add x9, x9, :lo12:ramdisk_storage
    add x9, x9, x0
    mov x10, #0
1:
    cmp x10, x2
    b.ge 2f
    ldrb w11, [x9, x10]
    strb w11, [x1, x10]
    add x10, x10, #1
    b 1b
2:
    ret

    // x0 = byte_offset, x1 = src ptr, x2 = nbytes
    .global mlnix_ramdisk_copy_in
mlnix_ramdisk_copy_in:
    adrp x9, ramdisk_storage
    add x9, x9, :lo12:ramdisk_storage
    add x9, x9, x0
    mov x10, #0
1:
    cmp x10, x2
    b.ge 2f
    ldrb w11, [x1, x10]
    strb w11, [x9, x10]
    add x10, x10, #1
    b 1b
2:
    ret
