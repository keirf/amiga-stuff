# mk_adf.py <bootblock> <payload> <output_adf>
#
# Stuff a given bootblock and payload into an output ADF image.
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import struct, sys

# Amiga bootblock checksum
def checksum(bb, sum=0):
    while len(bb):
        x, bb = struct.unpack(">L",bb[:4]), bb[4:]
        sum += x[0]
        if sum >= (1<<32):
            sum -= (1<<32)-1
    return sum

def main(argv):
    with open(argv[1], "rb") as f:
        bb_dat = f.read()
    with open(argv[2], "rb") as f:
        pl_dat = f.read()

    bb_len = len(bb_dat) + 4
    assert (bb_len & 3) == 0
    
    _, decompressed_length, _, leeway = struct.unpack(">2I2H", pl_dat[:12])
    pl_dat = pl_dat[12:-2]
    pl_len = len(pl_dat)
    
    # Leeway must account for the padding at end of last block loaded.
    leeway += -(bb_len + pl_len) & 511

    # Length of data to load from disk (multiple of 512-byte blocks).
    load_len = (bb_len + pl_len + 511) & ~511

    # Amount of memory to allocate in the loader for unpacked payload.
    alloc_len = max(load_len, decompressed_length) + leeway
    alloc_len += -alloc_len & 3
    
    bb_dat += struct.pack(">2I", alloc_len-load_len, load_len)
    print(" BB + Compressed = %u + %u = %u"
          % (bb_len, pl_len, bb_len + pl_len))
    print(" Load - Compressed = %u - %u = %u"
          % (load_len, pl_len, load_len - pl_len))
    print(" Alloc - Decompressed = %u - %u = %u"
          % (alloc_len, decompressed_length, alloc_len - decompressed_length))
    print(" == %u Tracks ==" % ((load_len + 5631) // 5632))
    
    # Splice the computed checksum into the header
    sum = checksum(bb_dat[:1024]) ^ 0xFFFFFFFF
    bb_dat = bb_dat[:4] + struct.pack(">L", sum) + bb_dat[8:]

    image = bb_dat + pl_dat
    image += bytes(901120 - len(image))
    with open(argv[3], "wb") as f:
        f.write(image)

if __name__ == "__main__":
    main(sys.argv)
