# Amiga-Stuff: Native & Cross-Dev Code & Tools for Amigas


## SysTest

### Download the latest build of SysTest [here (Github)](https://github.com/keirf/Amiga-Stuff/releases/download/systest-v1.1/systest-v1.1.zip).

### Build From Source

Requires a GCC cross-compiler toolchain targetting
`m68k-unknown-elf`. I recommend binutils-2.28 and gcc-7.1.0, built
with the following configuration lines on a Linux host or VM (note these are
not exhaustive toolchain build instructions!):
```
# ../binutils-2.28/configure --prefix=/path/to/install --target=m68k-unknown-elf
# ../gcc-7.1.0/configure --prefix=/path/to/install --target=m68k-unknown-elf --enable-languages=c --disable-libssp
```

Note that `/path/to/install` must be on your PATH both when building
and using the cross compiler!

To build SysTest: `make systest`. The ADF and distribution ZIP file are now
in the systest/ folder.

## Summary

This is a selection of code that I will add to over time. There are
three main file types, distinguished by suffix:
- **.S**    CPP + GAS assembler syntax (for cross-dev environments)
- **.asm**  Amiga native asm syntax (Devpac, AsmOne, PhxAss, vasm, Barfly, ...)
- **.c**    Somewhat-portable C (I will gladly take patches to make the C code
            work outside a GCC/POSIX environment).

All code is public domain (see the COPYING file).


## File Descriptions

For detailed build and usage instructions, see
file header comments and run-time help info.

- **base/**
  GNU build rules, initialisation code, utility code and headers.

- **systest/**
  Amiga system tests, built as a Workbench/CLI executable and as a
  bootable disk image.

- **host_tools/kickconv**
  Convert Kickstart ROM images: byte-swap, word-split, decrypt, fix checksums.
  Especially useful for creating images for burning to EPROM.

- **attic/crc16_ccitt.S**
  Table-driven CRC16-CCITT implementation.

- **inflate/degzip_{gnu,portable}.c**
  Analyse gzip files, extract and write out their raw Deflate streams
  (can then be processed by inflate, see below).
  Original version for GNU/Linux, and portable version fixed by phx / EAB.

- **inflate/inflate.{S,asm}**
  Small and efficient implementation of Inflate, as specified
  in RFC 1951 "DEFLATE Compressed Data Format Specification".
