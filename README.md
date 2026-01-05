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

### Build From Source (Manual Method)

This is as simple as `make all` when all prerequisites are
installed. The ADF and distribution ZIP file will then be in the
testkit/ folder.

### Build Prerequisites

Pick a location to install your locally-built toolchains, and make sure it is
on your PATH. For example:
```
mkdir -p $HOME/install/bin
export PATH=$PATH:$HOME/install/bin
```

#### 1. m68k-elf toolchain

I recommend binutils-2.34 and gcc-9.3.0, or newer, built with the following
configuration lines on a Linux host or VM. Note these are not
exhaustive toolchain build instructions, as GCC itself has a large number
of prerequisites. You can use the `.github/workflow` scripts for further
hints.
```
../binutils-2.34/configure --prefix=/path/to/install --target=m68k-elf
../gcc-9.3.0/configure --prefix=/path/to/install --target=m68k-elf --enable-languages=c --disable-libssp
```

On macOS you can instead straightforwardly install from Homebrew:
```
brew install m68k-elf-gcc
```

#### 2. m68k-amigaos toolchain

Clone, build and install bebbo's [amiga-gcc toolchain
(GitHub)][bebbo]. Please follow the README instructions very
carefully. This is especially important if building on macOS, where
careful use of Homebrew-installed tools (including bison!) is required
to avoid cryptic build failures.

#### 3. Zopfli

Google's Zopfli is a gzip replacement. It can be installed using your OS package manager, for example `apt` (Ubuntu) or `brew` (macOS).

#### 4. amitools/xdftool

This is a Python package which can be installed using pip or pipx. For example:
```
pipx install amitools
```

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
[bebbo]: https://franke.ms/git/bebbo/amiga-gcc
