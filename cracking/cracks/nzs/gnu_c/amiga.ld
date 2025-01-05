ENTRY(start)

PHDRS
{
  /* Define as RW^X to avoid a linker warning */
  text PT_LOAD FLAGS(6);
}

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
  } :text

  .data : {
    . = ALIGN(4);
    _sdat = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edat = .;
  } :text

  .bss : {
    . = ALIGN(4);
    _sbss = .;
    *(.bss)
    *(.bss*)
    . = ALIGN(4);
    _ebss = .;
    _end = .;
  } :text

  /DISCARD/ : {
    *(.eh_frame)
  }
}
