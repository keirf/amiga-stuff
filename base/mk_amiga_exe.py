# mk_amiga_exe.py
#
# Convert a raw binary into a simple AmigaDOS executable
# file with one loadable hunk and no relocations
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import struct, sys

HUNK_CODE   = 0x3e9
HUNK_END    = 0x3f2
HUNK_HEADER = 0x3f3

def main(argv):
    in_f = open(argv[1], "rb")
    out_f = open(argv[2], "wb")
    in_dat = in_f.read()
    in_len = len(in_dat)
    assert (in_len & 3) == 0, "input is not longword padded"
    out_f.write(struct.pack(">LLLLLL", HUNK_HEADER, 0, 1, 0, 0, in_len//4))
    out_f.write(struct.pack(">LL", HUNK_CODE, in_len//4))
    out_f.write(in_dat)
    out_f.write(struct.pack(">L", HUNK_END))

if __name__ == "__main__":
    main(sys.argv)
