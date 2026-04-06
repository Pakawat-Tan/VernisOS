/* VernisOS Minimal C Library */
#include "syscall.h"
#include "libc.h"

/* ---- string functions ---- */

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++)) ;
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dst;
}

/* ---- I/O functions ---- */

int puts(const char *s) {
    int len = (int)strlen(s);
    write(1, s, (size_t)len);
    write(1, "\n", 1);
    return len + 1;
}

static void _put_uint(unsigned long val) {
    char buf[20];
    int i = 0;
    if (val == 0) { write(1, "0", 1); return; }
    while (val > 0) { buf[i++] = (char)('0' + val % 10); val /= 10; }
    for (int j = i - 1; j >= 0; j--) write(1, &buf[j], 1);
}

static void _put_int(long val) {
    if (val < 0) { write(1, "-", 1); _put_uint((unsigned long)(-val)); }
    else _put_uint((unsigned long)val);
}

static void _put_hex(unsigned long val) {
    write(1, "0x", 2);
    if (val == 0) { write(1, "0", 1); return; }
    char buf[16]; int i = 0;
    while (val > 0) {
        int nib = (int)(val & 0xF);
        buf[i++] = nib < 10 ? (char)('0' + nib) : (char)('a' + nib - 10);
        val >>= 4;
    }
    for (int j = i - 1; j >= 0; j--) write(1, &buf[j], 1);
}

int printf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int count = 0;
    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] == '%' && fmt[i+1]) {
            i++;
            if (fmt[i] == 's') {
                const char *s = __builtin_va_arg(ap, const char *);
                if (!s) s = "(null)";
                int len = (int)strlen(s);
                write(1, s, (size_t)len);
                count += len;
            } else if (fmt[i] == 'd') {
                long v = __builtin_va_arg(ap, long);
                _put_int(v);
                count++;
            } else if (fmt[i] == 'u') {
                unsigned long v = __builtin_va_arg(ap, unsigned long);
                _put_uint(v);
                count++;
            } else if (fmt[i] == 'x') {
                unsigned long v = __builtin_va_arg(ap, unsigned long);
                _put_hex(v);
                count++;
            } else if (fmt[i] == 'c') {
                int c = __builtin_va_arg(ap, int);
                char ch = (char)c;
                write(1, &ch, 1);
                count++;
            } else if (fmt[i] == '%') {
                write(1, "%", 1);
                count++;
            }
        } else {
            write(1, &fmt[i], 1);
            count++;
        }
    }
    __builtin_va_end(ap);
    return count;
}

/* ---- malloc / free (sbrk bump allocator) ---- */

static uint8_t *heap_cur = 0;

void *malloc(size_t size) {
    if (size == 0) return (void *)0;
    /* Align to 16 bytes */
    size = (size + 15) & ~(size_t)15;
    if (!heap_cur) {
        heap_cur = (uint8_t *)sbrk(0);
        if ((intptr_t)heap_cur < 0) return (void *)0;
    }
    void *ptr = heap_cur;
    void *result = sbrk((int)size);
    if ((intptr_t)result < 0) return (void *)0;
    heap_cur += size;
    return ptr;
}

void free(void *ptr) {
    (void)ptr; /* bump allocator — no free */
}
