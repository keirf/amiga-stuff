ENTRY(start)

SECTIONS
{
  GRAPHICS = 0x20000;
  SUPER_SP = 0x80000;
  USER_SP = 0x7fc00;
  . = 0x7e000;

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
