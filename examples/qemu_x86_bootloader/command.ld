ENTRY(command_start)

SECTIONS
{
    . = 0x00050000;
    .command :
    {
        KEEP(*(.command_entry))
        *(.text*)
        *(.rodata*)
        *(.data*)
    }
    .bss :
    {
        *(.bss*)
        *(COMMON)
    }
    ASSERT(. <= 0x00080000, "command exceeds the transient command area")
    /DISCARD/ :
    {
        *(.eh_frame*)
        *(.comment*)
        *(.note*)
    }
}
