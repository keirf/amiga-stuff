/*
 * util.h
 * 
 * General utility functions and definitions.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#if __GNUC__ < 3
#define attribute_used __attribute__((unused))
#define likely(x) x
#define unlikely(x) x
#else
#define attribute_used __attribute__((used))
#define likely(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)
#endif

#define barrier() asm volatile("" ::: "memory")

#ifndef offsetof
#define offsetof(a,b) __builtin_offsetof(a,b)
#endif
#define container_of(ptr, type, member) ({                      \
        typeof( ((type *)0)->member ) *__mptr = (ptr);          \
        (type *)( (char *)__mptr - offsetof(type,member) );})
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define min(x,y) ({                             \
    const typeof(x) _x = (x);                   \
    const typeof(y) _y = (y);                   \
    (void) (&_x == &_y);                        \
    _x < _y ? _x : _y; })

#define max(x,y) ({                             \
    const typeof(x) _x = (x);                   \
    const typeof(y) _y = (y);                   \
    (void) (&_x == &_y);                        \
    _x > _y ? _x : _y; })

#define min_t(type,x,y) \
    ({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })
#define max_t(type,x,y) \
    ({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })

void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);

int vsprintf(char *str, const char *format, va_list ap)
    __attribute__ ((format (printf, 2, 0)));

int sprintf(char *str, const char *format, ...)
    __attribute__ ((format (printf, 2, 0)));

#define do_div(x, y) ({                                             \
    uint32_t _x = (x), _y = (y), _q, _r;                            \
    asm volatile (                                                  \
        "swap %3; "                                                 \
        "move.w %3,%0; "                                            \
        "divu.w %4,%0; "  /* hi / div */                            \
        "move.w %0,%1; "  /* stash quotient-hi */                   \
        "swap %3; "                                                 \
        "move.w %3,%0; "  /* lo / div */                            \
        "divu.w %4,%0; "                                            \
        "swap %1; "                                                 \
        "move.w %0,%1; "  /* stash quotient-lo */                   \
        "eor.w %0,%0; "                                             \
        "swap %0; "                                                 \
        : "=&d" (_r), "=&d" (_q) : "0" (0), "d" (_x), "d" (_y));    \
   (x) = _q;                                                        \
   _r;                                                              \
})

/* Text/data/BSS address ranges. */
extern char _stext[], _etext[];
extern char _sdat[], _edat[], _ldat[];
extern char _sbss[], _ebss[];

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
