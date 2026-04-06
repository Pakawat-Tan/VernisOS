/* VernisOS Minimal C Library Header */
#ifndef VERNIS_USERLIB_LIBC_H
#define VERNIS_USERLIB_LIBC_H

#include <stddef.h>

size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);

int puts(const char *s);
int printf(const char *fmt, ...);

void *malloc(size_t size);
void free(void *ptr);

#endif