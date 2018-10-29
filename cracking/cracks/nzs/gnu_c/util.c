/*
 * util.c
 * 
 * General-purpose utility functions.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

int vsprintf(char *str, const char *format, va_list ap)
{
    unsigned int x;
    short width;
    char c, *p = str, tmp[12], *q;

    while ((c = *format++) != '\0') {
        if (c != '%') {
            *p++ = c;
            continue;
        }

        width = 0;
    more:
        switch (c = *format++) {
        case '1'...'9':
            width = c-'0';
            while (((c = *format) >= '0') && (c <= '9')) {
                width = width*10 + c-'0';
                format++;
            }
            goto more;
        case 'd':
        case 'u':
            break;
        case 's':
            q = va_arg(ap, char *);
            while ((c = *q++) != '\0') {
                *p++ = c;
                width--;
            }
            while (width-- > 0)
                *p++ = '.';
            continue;
        case 'c':
            c = va_arg(ap, unsigned int);
        default:
            *p++ = c;
            continue;
        }

        x = va_arg(ap, unsigned int);

        if (x == 0) {
            q = tmp;
            *q++ = '0';
        } else {
            for (q = tmp; x; q++)
                *q = '0' + do_div(x, 10);
        }
        while (width-- > (q-tmp))
            *p++ = ' ';
        while (q != tmp)
            *p++ = *--q;
    };

    *p = '\0';

    return p - str;
}

int sprintf(char *str, const char *format, ...)
{
    va_list ap;
    int n;

    va_start(ap, format);
    n = vsprintf(str, format, ap);
    va_end(ap);

    return n;
}

void *memset(void *s, int c, size_t n)
{
    char *p = s;
    while (n--)
        *p++ = c;
    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    char *p = dest;
    const char *q = src;
    while (n--)
        *p++ = *q++;
    return dest;
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
