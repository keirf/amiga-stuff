/*
 * kickmem.c
 * 
 * Kickstart detection.
 * 
 * Written & released by Rene F. <fook@gmx.net>
 * Modified by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#include "testkit.h"

static struct amiga_kickstart {
    uint16_t magic;       /* 0x1111, 1114, .... */
    uint16_t entry_jmp;	  /* 0x4ef9 */
    uint32_t entry_adr;
    uint16_t reserved1;	  /* 0x0000 */
    uint16_t reserved2;	  /* 0xffff */
    uint16_t ver_major;
    uint16_t ver_minor;
} * const kickmem = (struct amiga_kickstart *)0xf80000;

/* Kickstart versions according to https://de.wikipedia.org/wiki/Kickstart */
static struct kick_ver {
    uint16_t ver_major, ver_minor;
    char str[9];
} kickstart_versions[] = {
    { 30,  -1, "1.0" },
    { 31,  -1, "1.1 NTSC" },
    { 32,  -1, "1.1 PAL" },
    { 33,  -1, "1.2" },
    { 34,  -1, "1.3" },
    { 35,  -1, "1.3 2024" },
    { 36,  -1, "1.4 beta" },
    { 37, 175, "2.04" },
    { 37, 299, "2.05" },
    { 37,  -1, "2.0x" },
    { 39,  -1, "3.0" },
    { 40,  -1, "3.1" },
    { 42,  -1, "3.2" },
    { 43,  -1, "3.1patch" },
    { 44,  -1, "3.5" },
    { 45,  -1, "3.9" },
    { 46,  -1, "3.1.4" },
    { 47,  -1, "3.2" },
    { 50,  -1, "MorphOS1" },
    { 51,  -1, "MorphOS2" },
    { 52,  -1, "4.0" },
    { 53,  -1, "4.1" }
};

const char *get_kick_string(void)
{
    static char kick_string[32];

    struct kick_ver *kick_ver;
    int i;

    /* Already detected Kickstart? Just return it again. */
    if (*kick_string != '\0')
        return kick_string;

    /* Basic sanity check for what a Kickstart ROM header looks like. */
    if ((kickmem->reserved1 != 0x0000)
        || (kickmem->reserved2 != 0xffff)
        || (kickmem->ver_major > 99)
        || (kickmem->ver_minor > 999)) {
        strcpy(kick_string, "Kickstart Not Found");
        return kick_string;
    }

    for (i = 0; i < ARRAY_SIZE(kickstart_versions); i++ )
    {
        kick_ver = &kickstart_versions[i];
        if ((kick_ver->ver_major == kickmem->ver_major)
            && ((kick_ver->ver_minor == kickmem->ver_minor)
                || (kick_ver->ver_minor == 0xffffu)))
            goto found;
    }

    sprintf(kick_string, "Unknown Kickstart (%u.%u)",
            kickmem->ver_major, kickmem->ver_minor);
    return kick_string;

found:
    sprintf(kick_string, "Kickstart %s (%u.%u)",
            kick_ver->str, kickmem->ver_major, kickmem->ver_minor);
    return kick_string;
}
