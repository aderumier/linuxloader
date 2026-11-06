#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>

// The Linux games' wide strings are 32 bits per character, and Windows'
// wchar_t 16: its wide functions stop at a game string's first character. Those
// the bridges cannot write in a line, as glibc does them (with the C locale's
// multibyte strings, one byte a character).

int wide32Vsnprintf(uint32_t *s, size_t n, const uint32_t *fmt, va_list args);
int wide32Vfprintf(FILE *stream, const uint32_t *fmt, va_list args);
int wide32Vsscanf(const uint32_t *s, const uint32_t *fmt, va_list args);

uint32_t *wide32Cat(uint32_t *dst, const uint32_t *src);
uint32_t *wide32Ncat(uint32_t *dst, const uint32_t *src, size_t n);
int wide32Casecmp(const uint32_t *a, const uint32_t *b);
uint32_t *wide32Tok(uint32_t *s, const uint32_t *delims, uint32_t **save);
long long wide32Toll(const uint32_t *s, uint32_t **end, int base);
unsigned long long wide32Toull(const uint32_t *s, uint32_t **end, int base);
