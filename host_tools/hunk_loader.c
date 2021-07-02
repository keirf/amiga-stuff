/*
 * hunk_loader.c
 * 
 * Analyse and load Amiga Hunk format executables.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>

#if defined(__APPLE__) && defined(__MACH__)
#include <libkern/OSByteOrder.h>
#define htobe32(x) OSSwapHostToBigInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#endif

#define HUNK_HEADER       0x3F3
#define HUNK_UNIT         0x3E7

#define HUNK_CODE         0x3E9
#define HUNK_DATA         0x3EA
#define HUNK_BSS          0x3EB

#define HUNK_RELOC32      0x3EC
#define HUNK_RELOC32SHORT 0x3FC
#define HUNK_RELOC16      0x3ED
#define HUNK_RELOC8       0x3EE
#define HUNK_DREL32       0x3F7
#define HUNK_DREL16       0x3F8
#define HUNK_DREL8        0x3F9
#define HUNK_ABSRELOC16   0x3FD
#define HUNK_SYMBOL       0x3F0
#define HUNK_DEBUG        0x3F1
#define HUNK_END          0x3F2
#define HUNK_EXT          0x3EF
#define HUNK_OVERLAY      0x3F5
#define HUNK_BREAK        0x3F6
#define HUNK_LIB          0x3FA
#define HUNK_INDEX        0x3FB

/* __packed: Given struct should not include ABI-compliant padding. */
#define __packed __attribute__((packed))

#define ASSERT(p) do { \
    if (!(p)) errx(1, "Assertion at %u", __LINE__); } while (0)

static uint32_t fetch32(char **in)
{
    uint32_t x = be32toh(*(uint32_t *)*in);
    *in += 4;
    return x;
}

static void usage(int rc)
{
    printf("Usage: hunk_loader [options] in_file out_file\n");
    printf("Options:\n");
    printf("  -h, --help    Display this information\n");
    printf("  -b, --base=N  Base address for load\n");
    printf("  -r, --reloc   Perform relocs\n");
    exit(rc);
}

int main(int argc, char **argv)
{
    int ch, fd, do_reloc = 0;
    uint32_t hunk_id, insz, outsz, i, x, first, last;
    uint32_t *hunk_offs, cur = 0;
    char *in, *out, *p, *buf, *outbuf;
    uint32_t base = 0;
    const static char *typename[] = { "Any", "Chip", "Fast", "Reserved" };
    const static char *hunkname[] = { "HUNK_CODE", "HUNK_DATA", "HUNK_BSS" };
    int seen_dat;

    const static char sopts[] = "hb:r";
    const static struct option lopts[] = {
        { "help", 0, NULL, 'h' },
        { "base", 1, NULL, 'b' },
        { "reloc", 0, NULL, 'r' },
        { 0, 0, 0, 0 }
    };

    while ((ch = getopt_long(argc, argv, sopts, lopts, NULL)) != -1) {
        switch (ch) {
        case 'h':
            usage(0);
            break;
        case 'b':
            base = strtol(optarg, NULL, 16);
            break;
        case 'r':
            do_reloc = 1;
            break;
        default:
            usage(1);
            break;
        }
    }

    if (argc != (optind + 2))
        usage(1);
    in = argv[optind];
    out = argv[optind+1];

    fd = open(in, O_RDONLY);
    if (fd == -1)
        err(1, "%s", in);
    if ((insz = lseek(fd, 0, SEEK_END)) < 0)
        err(1, NULL);
    lseek(fd, 0, SEEK_SET);
    if ((buf = malloc(insz)) == NULL)
        err(1, NULL);
    if (read(fd, buf, insz) != insz)
        err(1, NULL);
    close(fd);
    p = buf;

    hunk_id = fetch32(&p);
    if (hunk_id != HUNK_HEADER)
        goto bad_hunk;
    printf("HUNK_HEADER\n");
    x = fetch32(&p);
    if (x)
        goto bad_hunk;
    x = fetch32(&p);
    first = fetch32(&p);
    last = fetch32(&p);
    if (first || (x != (last+1-first))) {
        /* For sanity's sake we expect only loadable hunks, numbered 0..N-1. */
        printf(" Table size: %u, First: %u, Last: %u\n", x, first, last);
        goto bad_hunk;
    }
    hunk_offs = malloc(x * sizeof(*hunk_offs));
    memset(hunk_offs, 0, x * sizeof(*hunk_offs));
    hunk_offs[0] = base;
    for (i = first; i <= last; i++) {
        uint8_t type;
        x = fetch32(&p);
        type = x >> 30;
        x &= (1u<<30)-1;
        if (type >= 3) {
            /* We don't support extended AllocMem flags (type=3) */
            printf("Bad hunk AllocFlag %u\n", type);
            goto bad_hunk;
        }
        printf("  Hunk %u: %u longwords (%s)\n", i, x, typename[type]);
        /* Real Amiga loader would AllocMem() here. */
        hunk_offs[i+1] = hunk_offs[i] + 4*x;
    }
    outsz = hunk_offs[i];
    outbuf = malloc(outsz);
    
    cur = seen_dat = 0;
    while ((p - buf) < insz) {
        hunk_id = fetch32(&p) & ((1u<<30)-1);
        switch (hunk_id) {
        case HUNK_CODE:
        case HUNK_DATA:
        case HUNK_BSS:
            if (seen_dat)
                cur++;
            seen_dat = 1;
            printf("\n%s [Hunk %u]\n", hunkname[hunk_id-HUNK_CODE], cur);
            x = fetch32(&p);
            printf("  %u longwords\n", x);
            if (hunk_id == HUNK_BSS) {
                memset(outbuf + hunk_offs[cur] - base, 0, 4*x);
            } else {
                memcpy(outbuf + hunk_offs[cur] - base, p, 4*x);
                p += 4*x;
            }
            break;
        case HUNK_RELOC32: {
            uint32_t nr, id;
            printf("HUNK_RELOC32\n");
            while ((nr = fetch32(&p)) != 0) {
                id = fetch32(&p);
                printf("  Hunk %u: %u offsets\n", id, nr);
                for (i = 0; i < nr; i++) {
                    x = fetch32(&p);
                    if (do_reloc) {
                        uint32_t *pp = (uint32_t *)(outbuf + hunk_offs[cur]
                                                    + x - base);
                        *pp = htobe32(be32toh(*pp) + hunk_offs[id]);
                    }
                }
            }
            break;
        }
        case HUNK_END:
            printf("HUNK_END\n");
            if (!seen_dat) {
                printf("Premature HUNK_END\n");
                goto bad_hunk;
            }
            cur++;
            seen_dat = 0;
            break;
        default:
            printf("%08x - UNKNOWN\n", hunk_id);
            goto bad_hunk;
        }
    }

    if (cur != (last+1))
        goto bad_hunk;

    fd = open(out, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd == -1)
        err(1, "%s", out);
    if (write(fd, outbuf, outsz) != outsz)
        err(1, NULL);
    close(fd);

    return 0;

bad_hunk:
    printf("ERROR_BAD_HUNK\n");
    return 1;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
