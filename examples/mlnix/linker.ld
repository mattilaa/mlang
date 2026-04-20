ENTRY(_start)

SECTIONS
{
  . = 0x40080000;

  .text : ALIGN(4096) {
    *(.text.boot)
    *(.text*)
    *(.rodata*)
  }

  .data : ALIGN(4096) {
    *(.data*)
  }

  .bss : ALIGN(4096) {
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;
  }

  /DISCARD/ : {
    *(.comment*)
    *(.note*)
    *(.eh_frame*)
    *(.llvm_addrsig)
  }
}
