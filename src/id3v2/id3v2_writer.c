/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#include "id3v2_writer.h"
#include "id3v2_defs.h"
#include "../../include/mp3tag/mp3tag_error.h"
#include "../util/string_util.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Frame serialization helpers                                        */
/* ------------------------------------------------------------------ */

/*
 * Write a frame header into the buffer.
 * Uses ID3v2.4 syncsafe frame sizes.
 */
static int write_frame_header(dyn_buffer_t *buf, const char *frame_id,
                              uint32_t body_size)
{
    uint8_t hdr[ID3V2_FRAME_HEADER_SIZE];
    memcpy(hdr, frame_id, 4);
    id3v2_syncsafe_encode(body_size, hdr + 4);
    hdr[8] = 0;  /* flags */
    hdr[9] = 0;
    return buffer_append(buf, hdr, ID3V2_FRAME_HEADER_SIZE);
}

/*
 * Serialize a text frame: encoding(1) + UTF-8 text.
 */
static int serialize_text_frame(dyn_buffer_t *buf, const char *frame_id,
                                const char *text)
{
    size_t text_len = strlen(text);
    uint32_t body_size = 1 + (uint32_t)text_len;

    if (write_frame_header(buf, frame_id, body_size) != 0)
        return -1;

    /* Encoding: UTF-8 */
    if (buffer_append_byte(buf, ID3V2_ENC_UTF8) != 0)
        return -1;

    return buffer_append(buf, text, text_len);
}

/*
 * Serialize a TXXX frame: encoding(1) + description(NUL) + value.
 */
static int serialize_txxx_frame(dyn_buffer_t *buf, const char *desc,
                                const char *text)
{
    size_t desc_len = strlen(desc);
    size_t text_len = text ? strlen(text) : 0;
    uint32_t body_size = 1 + (uint32_t)desc_len + 1 + (uint32_t)text_len;

    if (write_frame_header(buf, "TXXX", body_size) != 0)
        return -1;

    if (buffer_append_byte(buf, ID3V2_ENC_UTF8) != 0)
        return -1;
    if (buffer_append(buf, desc, desc_len) != 0)
        return -1;
    if (buffer_append_byte(buf, 0) != 0)  /* NUL separator */
        return -1;
    if (text_len > 0 && buffer_append(buf, text, text_len) != 0)
        return -1;

    return 0;
}

/*
 * Serialize a COMM frame: encoding(1) + language(3) + description(NUL) + text.
 */
static int serialize_comm_frame(dyn_buffer_t *buf, const char *text,
                                const char *language)
{
    const char *lang = (language && language[0]) ? language : "und";
    size_t text_len = text ? strlen(text) : 0;
    /* encoding(1) + lang(3) + empty_description(1 NUL) + text */
    uint32_t body_size = 1 + 3 + 1 + (uint32_t)text_len;

    if (write_frame_header(buf, "COMM", body_size) != 0)
        return -1;

    if (buffer_append_byte(buf, ID3V2_ENC_UTF8) != 0)
        return -1;
    /* Language code (exactly 3 bytes) */
    char lang3[3] = {lang[0], lang[1] ? lang[1] : ' ', lang[2] ? lang[2] : ' '};
    if (buffer_append(buf, lang3, 3) != 0)
        return -1;
    /* Empty short description + NUL */
    if (buffer_append_byte(buf, 0) != 0)
        return -1;
    /* Text */
    if (text_len > 0 && buffer_append(buf, text, text_len) != 0)
        return -1;

    return 0;
}

/*
 * Serialize a binary frame: raw data as-is.
 */
static int serialize_binary_frame(dyn_buffer_t *buf, const char *frame_id,
                                  const uint8_t *data, size_t size)
{
    if (write_frame_header(buf, frame_id, (uint32_t)size) != 0)
        return -1;
    return buffer_append(buf, data, size);
}

/*
 * Check if a name looks like a valid 4-character frame ID.
 */
static int is_frame_id(const char *name)
{
    if (!name) return 0;
    int len = 0;
    for (const char *p = name; *p; p++, len++) {
        if (len >= 4) return 0;
        char c = *p;
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            return 0;
    }
    return len == 4;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

int id3v2_serialize_frames(const mp3tag_collection_t *coll, dyn_buffer_t *buf)
{
    if (!coll || !buf)
        return MP3TAG_ERR_INVALID_ARG;

    for (const mp3tag_tag_t *tag = coll->tags; tag; tag = tag->next) {
        for (const mp3tag_simple_tag_t *st = tag->simple_tags; st; st = st->next) {
            if (!st->name)
                continue;

            /* Binary tag */
            if (st->binary && st->binary_size > 0) {
                /* Use frame ID from name if valid, else use TXXX binary? */
                if (is_frame_id(st->name)) {
                    if (serialize_binary_frame(buf, st->name,
                                               st->binary, st->binary_size) != 0)
                        return MP3TAG_ERR_NO_MEMORY;
                }
                /* Skip binary data with non-frame-ID names (no standard way) */
                continue;
            }

            /* No value -> skip */
            if (!st->value)
                continue;

            /* Check for COMMENT -> COMM */
            if (str_casecmp(st->name, "COMMENT") == 0) {
                if (serialize_comm_frame(buf, st->value, st->language) != 0)
                    return MP3TAG_ERR_NO_MEMORY;
                continue;
            }

            /* Look up the standard frame ID */
            const char *frame_id = id3v2_name_to_frame_id(st->name);

            if (frame_id) {
                if (serialize_text_frame(buf, frame_id, st->value) != 0)
                    return MP3TAG_ERR_NO_MEMORY;
            } else if (is_frame_id(st->name)) {
                /* Name is a raw frame ID — serialize as text frame */
                if (serialize_text_frame(buf, st->name, st->value) != 0)
                    return MP3TAG_ERR_NO_MEMORY;
            } else {
                /* Unknown name -> TXXX with name as description */
                if (serialize_txxx_frame(buf, st->name, st->value) != 0)
                    return MP3TAG_ERR_NO_MEMORY;
            }
        }
    }

    return MP3TAG_OK;
}

void id3v2_build_header(uint32_t body_size, uint8_t hdr_out[10])
{
    hdr_out[0] = 'I';
    hdr_out[1] = 'D';
    hdr_out[2] = '3';
    hdr_out[3] = 4;    /* Version 2.4 */
    hdr_out[4] = 0;    /* Revision */
    hdr_out[5] = 0;    /* Flags: none */
    id3v2_syncsafe_encode(body_size, hdr_out + 6);
}
