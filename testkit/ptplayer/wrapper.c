/*
 * ptplayer/wrapper.c
 * 
 * GNU C wrappers around the PT Player routines.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

extern uint8_t _mt_Enable;

void mt_install_cia(int is_pal);
asm (
    "mt_install_cia:\n"
    "    move.l %a6,-(%sp)       \n"
    "    lea.l  (0xdff000).l,%a6 \n"
    "    sub.l  %a0,%a0          \n"
    "    move.l 8(%sp),%d0       \n"
    "    jbsr   _mt_install_cia  \n"
    "    move.l (%sp)+,%a6       \n"
    "    rts                     \n"
    );

void mt_init(void *mod, int songpos);
asm (
    "mt_init:\n"
    "    move.l %a6,-(%sp)       \n"
    "    lea.l  (0xdff000).l,%a6 \n"
    "    move.l 8(%sp),%a0       \n"
    "    sub.l  %a1,%a1          \n"
    "    move.l 12(%sp),%d0      \n"
    "    jbsr   _mt_init         \n"
    "    move.l (%sp)+,%a6       \n"
    "    rts                     \n"
    );

void mt_end(void);
asm (
    "mt_end:\n"
    "    move.l %a6,-(%sp)       \n"
    "    lea.l  (0xdff000).l,%a6 \n"
    "    jbsr   _mt_end          \n"
    "    move.l (%sp)+,%a6       \n"
    "    rts                     \n"
    );

void mt_remove_cia(void);
asm (
    "mt_remove_cia:\n"
    "    move.l %a6,-(%sp)       \n"
    "    lea.l  (0xdff000).l,%a6 \n"
    "    jbsr   _mt_remove_cia   \n"
    "    move.l (%sp)+,%a6       \n"
    "    rts                     \n"
    );

void mt_disablemask(unsigned int mask);
asm (
    "mt_disablemask:\n"
    "    move.l %a6,-(%sp)       \n"
    "    lea.l  (0xdff000).l,%a6 \n"
    "    move.l 8(%sp),%d0       \n"
    "    jbsr   _mt_disablemask  \n"
    "    move.l (%sp)+,%a6       \n"
    "    rts                     \n"
    );
