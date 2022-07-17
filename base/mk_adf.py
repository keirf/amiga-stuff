# mk_adf.py <bootblock> <payload> <output_adf>
#
# Stuff a given bootblock and payload into an output ADF image.
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import struct, sys

def alloc_in_bitmap(image, start, nr):
    # Bootblock is reserved and excluded from bitmap
    start -= 2
    if start < 0:
        nr += start
        start = 0
    # Bitmap block index is 79th longword in the root block
    off = 880*512 + 79*4
    bm_block, = struct.unpack(">L", image[off:off+4])
    # Update the bitmap one longword at a time
    while nr > 0:
        # Create bitmask of blocks we're allocating in this longword
        x = ((1 << (start & 31)) - 1) ^ 0xFFFFFFFF
        x &= (1 << min(nr + (start & 31), 32)) - 1
        # Extract the current value of this longword from the bitmap
        off = bm_block * 512 + 4 + (start//32)*4
        y = struct.unpack(">L", image[off:off+4])[0]
        # Assert that bits we're allocating are currently free
        assert x&y == x
        # Write back the modified bitmap longword
        image[off:off+4] = struct.pack(">L", (x^0xFFFFFFFF)&y)
        nr -= 32 - (start & 31)
        start += 32 - (start & 31)
    # Now recalculate the bitmap block's checksum
    off = bm_block * 512
    image[off:off+4] = struct.pack(">L", block_checksum(image[off+4:off+512]))

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
    alloc_in_bitmap(image, 0, nr_blocks)

    # AmigaTestKit writes to the last two tracks in the floppy write test.
    # Allocate those tracks in the filesystem bitmap.
    alloc_in_bitmap(image, 158*11, 2*11)

    with open(argv[3], "wb") as f:
        f.write(image)

if __name__ == "__main__":
    main(sys.argv)
