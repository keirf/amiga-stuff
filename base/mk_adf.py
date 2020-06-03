# mk_adf.py <bootblock> <payload> <output_adf>
#
# Stuff a given bootblock and payload into an output ADF image.
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import struct, sys, os, re

# Amiga bootblock checksum
def checksum(bb, sum=0):
    while len(bb):
        x, bb = struct.unpack(">L",bb[:4]), bb[4:]
        sum += x[0]
        if sum >= (1<<32):
            sum -= (1<<32)-1
    return sum

def main(argv):
    bb, pl, output = tuple(argv[1:4])
    
    with open(bb, "rb") as f:
        bb_dat = f.read()
    bb_len = len(bb_dat) + 4
    assert (bb_len & 3) == 0

    decompressed_length = os.path.getsize(pl)
    os.system('Shrinkler --data %s %s.tmp | tee %s.log.tmp' % (pl, pl, pl))
    with open(pl+'.tmp', "rb") as f:
        pl_dat = f.read()
    pl_len = len(pl_dat)
    with open(pl+'.log.tmp', 'r') as f:
        leeway = int(re.search(
            'Minimum safety margin for overlapped decrunching: (-?[0-9]+)',
            f.read()).group(1))
    print(leeway)
    os.system('rm %s.tmp %s.log.tmp' % (pl, pl))

    # Leeway must account for the padding at end of last block loaded.
    leeway += -(bb_len + pl_len) & 511
    leeway = max(leeway, 0)

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
    image = bb_dat + pl_dat
    sum = checksum(image[:1024]) ^ 0xFFFFFFFF
    image = image[:4] + struct.pack(">L", sum) + image[8:]

    image += bytes(901120 - len(image))
    with open(output, "wb") as f:
        f.write(image)

if __name__ == "__main__":
    main(sys.argv)
