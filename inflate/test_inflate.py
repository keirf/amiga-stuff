# test_inflate.py
# 
# Test harness for inflate.S
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import crcmod.predefined
import struct, sys, os

# Command for creating gzip files.
GZIP = "zopfli -c"
#GZIP = "gzip -c9"

HUNK_HEADER = 0x3f3
HUNK_CODE   = 0x3e9
HUNK_END    = 0x3f2
PREFIX = '_test_'

def usage(argv):
    print("%s input-files..." % argv[0])
    sys.exit(1)

def main(argv):
    nr = 0
    for a in argv[1:]:
        if os.path.isfile(a):
            test(a)
            nr += 1
    print("** Tested %u OK" % nr)

def test(name):
    print("Testing '%s'..." % name)

    # _test_0: Local copy of original file
    os.system('cp "' + name + '" ' + PREFIX + '0')
    crc16 = crcmod.predefined.Crc('crc-ccitt-false')
    with open(PREFIX + '0', 'rb') as f:
        crc16.update(f.read())

    # _test_1: Gzipped file
    os.system(GZIP + ' ' + PREFIX + '0 >' + PREFIX + '1')

    # _test_2: DEFLATE stream + custom header/footer
    os.system('degzip -H ' + PREFIX + '1 ' + PREFIX + '2')

    # _test_3: Register & memory state for 68000 emulator
    f = open(PREFIX + '3', 'wb')
    # Poison exception vectors and low memory to 0x1000
    mem = bytearray([0xde,0xad,0xbe,0xef]) * 0x400
    # Marshal the depacker and test harness (Amiga load file)
    with open('test_inflate', 'rb') as infile:
        (id, x, nr, first, last) = struct.unpack('>5I', infile.read(5*4))
        assert id == HUNK_HEADER and x == 0
        assert nr == 1 and first == 0 and last == 0
        (x, id, nr) = struct.unpack('>3I', infile.read(3*4))
        assert id == HUNK_CODE and nr == x
        mem += infile.read(nr * 4)
        (id,) = struct.unpack('>I', infile.read(4))
        assert id == HUNK_END
    # Marshal the compressed binary with header (padded to longword)
    with open(PREFIX + '2', 'rb') as infile:
        mem += infile.read()
    mem += bytearray([0]) * (-len(mem)&3)
    # Poison remaining longwords to top of memory
    remain = (0x200000 - len(mem)) // 4
    mem += bytearray([0xde,0xad,0xbe,0xef]) * (remain-1)
    # Final return address is magic longword (we check PC after emulation)
    mem += bytearray([0xf0,0xe0,0xd0,0xc0])
    f.write(mem)
    # Initial register state: only SR,PC,SP really matter
    f.write(struct.pack('>18IH6x', 0,0,0,0,0,0,0,0, # D0-D7
                        0,0,0,0,0,0,0,0x1ffffc, # A0-A7
                        0x1000, # PC
                        0, # SSP
                        0)) # SR
    f.close()

    # Requires m68k_emulate from Github:keirf/Disk-Utilities.git/m68k on PATH
    os.system('m68k_emulate ' + PREFIX + '3 ' + PREFIX + '4')
    with open(PREFIX + '4', 'rb') as f:
        mem = bytes(f.read(0x200000))
        (d0,d1,d2,d3,d4,d5,d6,d7) = struct.unpack('>8I', f.read(8*4))
        (a0,a1,a2,a3,a4,a5,a6,a7) = struct.unpack('>8I', f.read(8*4))
        (pc,ssp,sr) = struct.unpack('>2IH6x', f.read(4*4))
        print('d0: %08x  d1: %08x  d2: %08x  d3: %08x '
              'd4: %08x  d5: %08x  d6: %08x  d7: %08x'
              % (d0,d1,d2,d3,d4,d5,d6,d7))
        print('a0: %08x  a1: %08x  a2: %08x  a3: %08x '
              'a4: %08x  a5: %08x  a6: %08x  a7: %08x'
              % (a0,a1,a2,a3,a4,a5,a6,a7))
        print('pc: %08x  ssp: %08x  sr: %04x' % (pc,ssp,sr))
        assert pc == 0xf0e0d0c0
        assert a7 == 0x200000
        assert d0&0xffff == crc16.crcValue # Emulated CRC == our CRC?
        assert d0&0xffff == d2&0xffff      # Emulated CRC == header CRC?
        assert sr&4 # CC_Z (ie crcs match)
        # Check CRC in situ in the returned memory buffer
        crc16.crcValue = 0xffff
        crc16.update(mem[a0:a0+d1])
        assert d0&0xffff == crc16.crcValue # Emulated CRC == our check?

    # All done: clean up
    print("")
    os.system('rm ' + PREFIX + '*')
    
if __name__ == "__main__":
    main(sys.argv)
