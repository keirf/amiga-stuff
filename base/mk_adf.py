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
    bb_f = open(argv[1], "rb")
    pl_f = open(argv[2], "rb")
    out_f = open(argv[3], "wb")
    bb_dat = bb_f.read()
    pl_dat = pl_f.read()

    # Construct bootblock header. We will splice in the checksum later.
    header = struct.pack(">4sLLLL",
                         b'DOS\0',                    # Bootblock signature
                         0,                           # Checksum (placeholder)
                         880,                         # Root block
                         0x60060000,                  # BRA.B +6
                         (len(pl_dat) + 511) & ~511)  # Payload length, padded
                         
    
    # Compute checksum over header, bootblock, and first 512 bytes of payload.
    sum = checksum(pl_dat[:512], checksum(bb_dat, checksum(header)))
    sum ^= 0xFFFFFFFF
    # Splice the computed checksum into the header
    header = header[:4] + struct.pack(">L", sum) + header[8:]
    # Write out the header and bootblock code
    out_f.write(header)
    out_f.write(bb_dat)
    # Pad bootblock to 512 bytes
    for x in range((512-len(bb_dat)-len(header))//4):
        out_f.write(struct.pack(">L", 0))
    # Write the payload from sector 1 onwards
    out_f.write(pl_dat)
    # If destination is an ADF, pad image to 880kB
    if "adf" in argv[3]:
        for x in range((901120-len(pl_dat)-512)//4):
            out_f.write(struct.pack(">L", 0))

if __name__ == "__main__":
    main(sys.argv)
