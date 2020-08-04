/*
 * kickmem.h
 * 
 * Kickstart offset definitions.
 * 
 * Written & released by Rene F. <fook@gmx.net>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

typedef union {
    void *p;
    uint32_t x;
} kickmem_vector_t;

struct amiga_kickstart {
	uint16_t magic;			// 0x1114
	uint16_t entry_jmp;		// 0x4EF9
	uint32_t entry_adr;		// 0x00F800D2
	uint16_t reserved1;		// 0x0000
	uint16_t reserved2;		// 0xFFFF
	uint16_t rom_version_major;
	uint16_t rom_version_minor;
};
