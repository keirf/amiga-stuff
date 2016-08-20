/*
 * kickconv.c
 * 
 * Byte-swap/split/dupe/decrypt Kickstart ROM files for EPROM programming.
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

static uint16_t _swap(uint16_t x)
{
    return (x >> 8) | (x << 8);
}

static uint32_t checksum(void *dat, unsigned int len)
{
    uint32_t csum = 0, *p = dat;
    unsigned int i;
    for (i = 0; i < len/4; i++) {
        uint32_t x = be32toh(p[i]);
        if ((csum + x) < csum)
            csum++;
        csum += x;
    }
    return ~csum;
}

static void usage(int rc)
{
    printf("Usage: kickconv [options] in_file out_file\n");
    printf("Options:\n");
    printf("  -h, --help      Display this information\n");
    printf("  -c, --csum      Fix bad ROM checksum\n");
    printf("  -i, --cd32-swizz  CD32-ROM swizzle\n");
    printf("  -I, --cd32-deswizz CD32-ROM de-swizzle\n");
    printf("  -k, --key=FILE  Key file for encrypted ROM\n");
    printf("  -s, --swap      Swap endianess (little vs big endian)\n");
    printf("  -S, --split     Split into two 16-bit ROM files\n");
    exit(rc);
}

int main(int argc, char **argv)
{
    int ch, fd, swap = 0, split = 0, fix_csum = 0;
    int insz, i, j, is_encrypted;
    uint8_t *buf, *outbuf[2], header[11];
    char *in, *out, *keyfile = NULL;
    uint32_t csum;
    enum { CD32_SWIZZLE = 1, CD32_DESWIZZLE = 2 } cd32_transform = 0;

    const static char sopts[] = "hciIk:sS";
    const static struct option lopts[] = {
        { "help", 0, NULL, 'h' },
        { "csum", 0, NULL, 'c' },
        { "cd32-swizz", 0, NULL, 'i' },
        { "cd32-deswizz", 0, NULL, 'I' },
        { "key", 1, NULL, 'k' },
        { "swap", 0, NULL, 's' },
        { "split", 0, NULL, 'S' },
        { 0, 0, 0, 0 }
    };

    while ((ch = getopt_long(argc, argv, sopts, lopts, NULL)) != -1) {
        switch (ch) {
        case 'h':
            usage(0);
            break;
        case 'c':
            fix_csum = 1;
            break;
        case 'i':
            cd32_transform = CD32_SWIZZLE;
            break;
        case 'I':
            cd32_transform = CD32_DESWIZZLE;
            break;
        case 'k':
            keyfile = optarg;
            break;
        case 's':
            swap = 1;
            break;
        case 'S':
            split = 1;
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
    if (insz < 8192)
        errx(1, "Input file too short");
    if (read(fd, header, sizeof(header)) != sizeof(header))
        err(1, NULL);
    is_encrypted = !strncmp((char *)header, "AMIROMTYPE1", sizeof(header));
    if (is_encrypted) {
        insz -= sizeof(header);
    } else {
        lseek(fd, 0, SEEK_SET);
    }
    if ((buf = malloc(insz)) == NULL)
        err(1, NULL);
    if (read(fd, buf, insz) != insz)
        err(1, NULL);
    close(fd);

    if (is_encrypted && !keyfile)
        errx(1, "Must specify keyfile for encrypted ROM");
    if (!is_encrypted && keyfile)
        errx(1, "Expected encrypted ROM");
    if ((insz & (insz-1)) != 0)
        errx(1, "Kickstart image must be power-of-two sized");

    printf("Input: '%s'", in);
    if (swap)
        printf("; Swap");
    if (split)
        printf("; Split");
    if (keyfile)
        printf("; Decrypt(\"%s\")", keyfile);
    if (cd32_transform)
        printf("; CD32-%s", (cd32_transform == CD32_SWIZZLE)
               ? "Swizzle" : "Deswizzle");
    printf("\n");

    if (keyfile) {
        unsigned char *key;
        int keysz;
        fd = open(keyfile, O_RDONLY);
        if (fd == -1)
            err(1, "%s", keyfile);
        if ((keysz = lseek(fd, 0, SEEK_END)) < 0)
            err(1, NULL);
        lseek(fd, 0, SEEK_SET);
        if ((key = malloc(keysz)) == NULL)
            err(1, NULL);
        if (read(fd, key, keysz) != keysz)
            err(1, NULL);
        close(fd);
        for (i = j = 0; i < insz; i++, j = (j + 1) % keysz)
            buf[i] ^= key[j];
        free(key);
    }

    csum = checksum(buf, insz);
    printf("ROM Checksum: %s", csum ? "BAD" : "OK");
    if (csum) {
        if (fix_csum) {
            *(uint32_t *)&buf[insz-24] = 0;
            *(uint32_t *)&buf[insz-24] = htobe32(checksum(buf, insz));
            printf(" (Fixed up: now OK)");
        } else {
            printf(" (Use option -c/--csum to fix it up)");
        }
    }
    putchar('\n');

    if (cd32_transform) {
        uint16_t *in = (uint16_t *)buf, *out = malloc(insz);
        if (cd32_transform == CD32_SWIZZLE) {
            for (i = 0; i < insz/2; i++)
                out[(i>>1)|((i&1)*(insz/4))] = in[i];
        } else {
            for (i = 0; i < insz/2; i++)
                out[i] = in[(i>>1)|((i&1)*(insz/4))];
        }
        free(buf);
        buf = (uint8_t *)out;
    }

    if (swap) {
        uint16_t *p = (uint16_t *)buf;
        for (i = 0; i < insz/2; i++, p++)
            *p = _swap(*p);
    }

    if (split) {
        char outname[256];
        uint16_t *a, *b, *p;
        a = (uint16_t *)(outbuf[0] = malloc(insz));
        b = (uint16_t *)(outbuf[1] = malloc(insz));
        for (j = 0; j < 2; j++) {
            p = (uint16_t *)buf;
            for (i = 0; i < insz/4; i++) {
                *a++ = *p++;
                *b++ = *p++;
            }
        }

        snprintf(outname, sizeof(outname), "%s_a", out);
        fd = open(outname, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if (fd == -1)
            err(1, "%s", outname);
        if (write(fd, outbuf[0], insz) != insz)
            err(1, NULL);
        close(fd);

        snprintf(outname, sizeof(outname), "%s_b", out);
        fd = open(outname, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if (fd == -1)
            err(1, "%s", outname);
        if (write(fd, outbuf[1], insz) != insz)
            err(1, NULL);
        close(fd);
    } else {
        fd = open(out, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if (fd == -1)
            err(1, "%s", out);
        if (write(fd, buf, insz) != insz)
            err(1, NULL);
        close(fd);
    }

    return 0;
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
