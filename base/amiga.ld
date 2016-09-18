ENTRY(start)

SECTIONS
{
  SUPER_SP = 0x40000;
  USER_SP = 0x3fc00;
  HEAP_END = 0x3f000;
  . = 0x400;

  .text : {
    _start = .;
    _stext = .;
    *(.text.init)
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
    _etext = .;
  }

  .data : {
    . = ALIGN(4);
    _sdat = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edat = .;
  }

  .bss : {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    . = ALIGN(4);
    _ebss = .;
    _end = .;
  }

  /DISCARD/ : {
    *(.eh_frame)
  }
}
