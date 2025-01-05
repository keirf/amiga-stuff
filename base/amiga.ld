ENTRY(start)

PHDRS
{
  /* Define as RW^X to avoid a linker warning */
  text PT_LOAD FLAGS(6);
}

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
