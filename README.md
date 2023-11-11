# Amiga Stuff: Code & Tools for Amigas

## Amiga Test Kit

![CI Badge][ci-badge]
![Downloads Badge][downloads-badge]
![Version Badge][version-badge]

### Download the latest release of Amiga Test Kit [here (Github)](https://github.com/keirf/amiga-stuff/releases/download/testkit-v1.21/AmigaTestKit-1.21.zip).

### Build From Source (Docker)

A Docker image has kindly been [supplied (Docker
Hub)](https://hub.docker.com/r/rjnorthrow/atk). You can run it as
follows to generate the latest Amiga Test Kit zip file in your current
directory:
```
docker run -v $(pwd):/output --rm -ti rjnorthrow/atk:latest
```

*COMMIT*, *MAKE_OPTS* and *atk version (v1.7, v1.10, latest)* may be
 set in the environment to build a particular version of Amiga Test
 Kit, and to specify extra build parameters:
```
docker run -e COMMIT=testkit-v1.3 -e MAKE_OPTS=-j4 -v $(pwd):/output --rm -ti rjnorthrow/atk:v1.7
```

For versions up to 1.7, use `rjnorthrow/atk:v1.7`. For versions 1.8 up to 1.10,
use `rjnorthrow/atk:v1.10`.

### Build From Source (Manual Method)

Requires a GCC cross-compiler toolchain targetting
`m68k-unknown-elf`. I recommend binutils-2.28 and gcc-7.1.0, built
with the following configuration lines on a Linux host or VM (note these are
not exhaustive toolchain build instructions!):
```
../binutils-2.28/configure --prefix=/path/to/install --target=m68k-unknown-elf
../gcc-7.1.0/configure --prefix=/path/to/install --target=m68k-unknown-elf --enable-languages=c --disable-libssp
```

Note that `/path/to/install/bin` must be on your PATH both when building
and using the cross compiler. For example:
```
mkdir -p $HOME/cross/bin
export PATH=$PATH:$HOME/cross/bin
... --prefix=$HOME/cross ...
```

The build also depends on Google's Zopfli (a gzip replacement). This can
be installed in Ubuntu Linux as follows:
```
sudo apt install zopfli
```

To build Amiga Test Kit: `make testkit`. The ADF and distribution ZIP file
are now in the testkit/ folder.

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

- **testkit/**
  Amiga Test Kit, built as a Workbench/CLI executable and as a
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

[ci-badge]: https://github.com/keirf/amiga-stuff/workflows/CI/badge.svg
[downloads-badge]: https://img.shields.io/github/downloads/keirf/amiga-stuff/total
[version-badge]: https://img.shields.io/github/v/release/keirf/amiga-stuff
