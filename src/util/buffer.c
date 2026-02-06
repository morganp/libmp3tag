/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#include "buffer.h"
#include <stdlib.h>
#include <string.h>

void buffer_init(dyn_buffer_t *buf)
{
    buf->data     = NULL;
    buf->size     = 0;
    buf->capacity = 0;
}

void buffer_free(dyn_buffer_t *buf)
{
    free(buf->data);
    buf->data     = NULL;
    buf->size     = 0;
    buf->capacity = 0;
}

int buffer_reserve(dyn_buffer_t *buf, size_t additional)
{
    size_t needed = buf->size + additional;
    if (needed <= buf->capacity)
        return 0;

    size_t new_cap = buf->capacity ? buf->capacity : 256;
    while (new_cap < needed)
        new_cap *= 2;

    uint8_t *new_data = realloc(buf->data, new_cap);
    if (!new_data)
        return -1;

    buf->data     = new_data;
    buf->capacity = new_cap;
    return 0;
}

int buffer_append(dyn_buffer_t *buf, const void *data, size_t size)
{
    if (size == 0)
        return 0;
    if (buffer_reserve(buf, size) != 0)
        return -1;
    memcpy(buf->data + buf->size, data, size);
    buf->size += size;
    return 0;
}

int buffer_append_byte(dyn_buffer_t *buf, uint8_t byte)
{
    return buffer_append(buf, &byte, 1);
}

int buffer_append_zeros(dyn_buffer_t *buf, size_t count)
{
    if (count == 0)
        return 0;
    if (buffer_reserve(buf, count) != 0)
        return -1;
    memset(buf->data + buf->size, 0, count);
    buf->size += count;
    return 0;
}
