#ifndef PENSHIP_UTF8_H
#define PENSHIP_UTF8_H

#include <stddef.h>

/* Decode one code point. Returns its byte length, or -1 if s[0..n) is not valid UTF-8. */
static inline int utf8_decode(const char *s, size_t n, unsigned *cp) {
    if (n == 0) {
        return -1;
    }
    const unsigned char *u = (const unsigned char *)s;
    unsigned char c = u[0];
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    if (c < 0xc2 || c > 0xf4) {
        return -1;
    }
    int need = c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
    if ((size_t)need > n) {
        return -1;
    }
    unsigned value = c & (0xff >> (need + 1));
    for (int i = 1; i < need; i++) {
        if ((u[i] & 0xc0) != 0x80) {
            return -1;
        }
        value = (value << 6) | (u[i] & 0x3f);
    }
    if (need == 3 && (c == 0xe0 && u[1] < 0xa0)) {
        return -1;
    }
    if (need == 3 && (c == 0xed && u[1] >= 0xa0)) {
        return -1;
    }
    if (need == 4 && (c == 0xf0 && u[1] < 0x90)) {
        return -1;
    }
    if (need == 4 && (c == 0xf4 && u[1] > 0x8f)) {
        return -1;
    }
    *cp = value;
    return need;
}

/* Encode cp into out, which must hold 4 bytes. Returns the length, or -1. */
static inline int utf8_encode(unsigned cp, char out[4]) {
    if (cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) {
        return -1;
    }
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xc0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xe0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[3] = (char)(0x80 | (cp & 0x3f));
    return 4;
}

/* 1 if every byte of s is valid UTF-8. */
static inline int utf8_valid(const char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned cp = 0;
        int len = utf8_decode(s + i, n - i, &cp);
        if (len < 0) {
            return 0;
        }
        i += (size_t)len;
    }
    return 1;
}

#endif
