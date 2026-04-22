ENTRY(_start)

SECTIONS
{
    . = 0x4e000000;

    .text : ALIGN(4096)
    {
        *(.text.boot)
        *(.text*)
    }

    .rodata : ALIGN(4096)
    {
        *(.rodata*)
    }

    . = ALIGN(0x200000);

    .data : ALIGN(4096)
    {
        *(.data*)
    }

    .bss (NOLOAD) : ALIGN(4096)
    {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        __bss_end = .;
    }
}
