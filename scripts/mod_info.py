# mod_info.py
# 
# Display information about a Protracker module.
# 
# Written & released by Keir Fraser <keir.xen@gmail.com>
# 
# This is free and unencumbered software released into the public domain.
# See the file COPYING for more details, or visit <http://unlicense.org>.

import struct, sys

with open(sys.argv[1], "rb") as f:
    dat = f.read()
dlen = len(dat)

tname, = struct.unpack("20s", dat[:20])
print("Name: '%s'" % tname.decode('utf-8'))
dat = dat[20:]
samples_len = 0
for i in range(31):
    name, wordlen, finetune, volume, repstart, replen = struct.unpack(
        ">22sH2B2H", dat[:30])
    dat = dat[30:]
    if wordlen == 0:
        continue
    samples_len += wordlen*2
print("Sample Data: %u" % samples_len)

songlen, pad = struct.unpack("2B", dat[:2])
dat = dat[2:]
#assert pad == 127
assert songlen <= 128
print("Song Length: %u" % songlen)

patterns = list(struct.unpack("128B", dat[:128]))
dat = dat[128:]
patterns = patterns[:songlen]
nr_patterns = max(patterns)+1
print("Nr Patterns: %u (%u bytes)" % (nr_patterns, nr_patterns*1024))

mksig, = struct.unpack("4s", dat[:4])
dat = dat[4:]
assert mksig == b'M.K.'

totlen = 1084 + nr_patterns*1024 + samples_len
print("Total Bytes: %u (0x%x)" % (totlen, totlen))
assert totlen <= dlen

