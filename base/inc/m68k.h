/*
 * m68k.h
 * 
 * M680x0 processor definitions.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

typedef union {
    void *p;
    uint32_t x;
} m68k_vector_t;

struct m68k_vector_table {
    m68k_vector_t reset_ssp;
    m68k_vector_t reset_pc;
    m68k_vector_t bus_error;
    m68k_vector_t address_error;
    m68k_vector_t illegal_instruction;
    m68k_vector_t zero_divide;
    m68k_vector_t chk_chk2;
    m68k_vector_t trapcc_trapv;
    m68k_vector_t privilege_violation;
    m68k_vector_t trace;
    m68k_vector_t a_line;
    m68k_vector_t f_line;
    m68k_vector_t _rsvd0[1];
    m68k_vector_t coprocessor_protocol_violation;
    m68k_vector_t format_error;
    m68k_vector_t uninitialised_interrupt;
    m68k_vector_t _rsvd1[8];
    m68k_vector_t spurious_interrupt;
    m68k_vector_t level1_autovector;
    m68k_vector_t level2_autovector;
    m68k_vector_t level3_autovector;
    m68k_vector_t level4_autovector;
    m68k_vector_t level5_autovector;
    m68k_vector_t level6_autovector;
    m68k_vector_t level7_autovector;
    m68k_vector_t trap[16];
    m68k_vector_t _rsvd2[16];
    m68k_vector_t user_interrupt[16];
};

/* An exception frame for entry to a normal C function (which may clobber
 * D0-D1/A0-A1). SR/PC are saved automatically during exception processing.
 * There may be other state saved beyond those two registers depending on the
 * exception type and CPU model. */
struct c_exception_frame {
    uint32_t d0, d1, a0, a1;
    uint16_t sr;
    uint32_t pc;
};

#define SR_TMASK  0xc000
#define SR_SUPER  0x2000
#define SR_MASTER 0x1000
#define SR_IMASK  0x0700
#define CCR_X     0x10
#define CCR_N     0x08
#define CCR_Z     0x04
#define CCR_V     0x02
#define CCR_C     0x01
