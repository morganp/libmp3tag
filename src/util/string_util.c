/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#include "string_util.h"
#include <stdlib.h>
#include <string.h>

char *str_dup(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (!copy) return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

int str_casecmp(const char *a, const char *b)
{
    if (!a || !b) return a != b;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return (unsigned char)ca - (unsigned char)cb;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int str_copy(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return -1;
    if (!src) {
        dst[0] = '\0';
        return 0;
    }
    size_t len = strlen(src);
    if (len >= dst_size) {
        memcpy(dst, src, dst_size - 1);
        dst[dst_size - 1] = '\0';
        return -1;  /* truncated */
    }
    memcpy(dst, src, len + 1);
    return 0;
}

void str_trim_fixed(char *dst, const char *src, size_t len)
{
    memcpy(dst, src, len);
    dst[len] = '\0';

    /* Trim trailing spaces and NUL bytes */
    size_t end = len;
    while (end > 0 && (dst[end - 1] == ' ' || dst[end - 1] == '\0'))
        end--;
    dst[end] = '\0';
}
