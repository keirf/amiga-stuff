# mk_adf.py <bootblock> <payload> <output_adf>
#
# Stuff a given bootblock and payload into an output ADF image.
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import struct, sys

def block_checksum(block, sum=0):
    xs = struct.unpack(">%dL" % (len(block)//4), block)
    for x in xs:
        sum += x
    return -sum & 0xffffffff

# Amiga bootblock checksum
def bootblock_checksum(bb, sum=0):
    xs = struct.unpack(">256L", bb[:1024])
    for x in xs:
        sum += x
        if sum >= (1<<32):
            sum -= (1<<32)-1
    return sum

def main(argv):
    with open(argv[1], "rb") as f:
        bb_dat = bytearray(f.read())
    with open(argv[2], "rb") as f:
        pl_dat = f.read()
    with open(argv[3], "rb") as f:
        image = bytearray(f.read())

    bb_len = len(bb_dat)
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

    print(" BB + Compressed = %u + %u = %u"
          % (bb_len, pl_len, bb_len + pl_len))
    print(" Load - Compressed = %u - %u = %u"
          % (load_len, pl_len, load_len - pl_len))
    print(" Alloc - Decompressed = %u - %u = %u"
          % (alloc_len, decompressed_length, alloc_len - decompressed_length))
    print(" == %u Tracks ==" % ((load_len + 5631) // 5632))
    
    # Concatenate everything and splice in the checksum.
    bb_dat[14:22] = struct.pack(">2I", alloc_len-load_len, load_len)
    dat = bytearray(bb_dat + pl_dat)
    dat[4:8] = struct.pack(">L", bootblock_checksum(dat) ^ 0xFFFFFFFF)

    # Splice the concatenated payload into the disk image.
    nr_blocks = (len(dat) + 511) // 512
    image[:len(dat)] = dat

    # Allocate our payload blocks in the filesystem bitmap.
    off = 880*512 + 79*4 # bitmap block idx is 79th longword in root block
    bm_block, = struct.unpack(">L", image[off:off+4])
    off = bm_block * 512 + 4 # skip first longword (checksum)
    nr_blocks -= 2 # bootblock is reserved and excluded from bitmap
    while nr_blocks != 0:
        n = min(nr_blocks, 32)
        image[off:off+4] = struct.pack(">L", ((1<<n)-1) ^ 0xFFFFFFFF)
        nr_blocks -= n
        off += 4
    # Now recalculate the bitmap block's checksum
    off = bm_block * 512
    image[off:off+4] = struct.pack(">L", block_checksum(image[off+4:off+512]))

    with open(argv[3], "wb") as f:
        f.write(image)

if __name__ == "__main__":
    main(sys.argv)
