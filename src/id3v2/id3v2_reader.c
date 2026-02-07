/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#include "id3v2_reader.h"
#include "id3v2_defs.h"
#include "../../include/mp3tag/mp3tag_error.h"
#include "../util/string_util.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Header parsing                                                     */
/* ------------------------------------------------------------------ */

int id3v2_read_header(file_handle_t *fh, int64_t offset, id3v2_header_t *hdr)
{
    if (!fh || !hdr)
        return MP3TAG_ERR_INVALID_ARG;

    uint8_t buf[ID3V2_HEADER_SIZE];
    if (file_seek(fh, offset) != 0)
        return MP3TAG_ERR_SEEK_FAILED;
    if (file_read(fh, buf, ID3V2_HEADER_SIZE) != 0)
        return MP3TAG_ERR_NOT_MP3;

    /* Check "ID3" magic */
    if (buf[0] != 'I' || buf[1] != 'D' || buf[2] != '3')
        return MP3TAG_ERR_NOT_MP3;

    /* We support v2.3 and v2.4 */
    if (buf[3] < 3 || buf[3] > 4)
        return MP3TAG_ERR_UNSUPPORTED;

    /* Validate syncsafe size bytes (each < 0x80) */
    for (int i = 6; i < 10; i++) {
        if (buf[i] & 0x80)
            return MP3TAG_ERR_BAD_ID3V2;
    }

    hdr->version_major    = buf[3];
    hdr->version_revision = buf[4];
    hdr->flags            = buf[5];
    hdr->tag_size         = id3v2_syncsafe_decode(buf + 6);
    hdr->has_footer        = (buf[3] == 4 && (buf[5] & ID3V2_FLAG_FOOTER)) ? 1 : 0;

    return MP3TAG_OK;
}

/* ------------------------------------------------------------------ */
/*  Text decoding helpers                                              */
/* ------------------------------------------------------------------ */

/*
 * Decode a text string from an ID3v2 text encoding to UTF-8.
 * Returns a newly allocated UTF-8 string, or NULL on failure.
 *
 * For simplicity, ISO-8859-1 is converted by zero-extending to UTF-8.
 * UTF-16 (BOM/BE) is converted to UTF-8.
 * UTF-8 is returned as-is.
 */
static char *decode_iso8859_1(const uint8_t *data, size_t len)
{
    /* Worst case: each byte becomes 2 UTF-8 bytes */
    char *out = malloc(len * 2 + 1);
    if (!out) return NULL;

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t c = data[i];
        if (c == 0) break;
        if (c < 0x80) {
            out[j++] = (char)c;
        } else {
            out[j++] = (char)(0xC0 | (c >> 6));
            out[j++] = (char)(0x80 | (c & 0x3F));
        }
    }
    out[j] = '\0';
    return out;
}

static char *decode_utf8(const uint8_t *data, size_t len)
{
    /* Find NUL terminator or use full length */
    size_t actual = 0;
    while (actual < len && data[actual] != 0)
        actual++;

    char *out = malloc(actual + 1);
    if (!out) return NULL;
    memcpy(out, data, actual);
    out[actual] = '\0';
    return out;
}

/*
 * Decode UTF-16 (LE or BE) to UTF-8.
 * If `bom` is non-zero, the first two bytes are a BOM.
 * If `bom` is zero and `default_be` is set, assume big-endian.
 */
static char *decode_utf16(const uint8_t *data, size_t len,
                          int has_bom, int default_be)
{
    if (len < 2) return str_dup("");

    int big_endian = default_be;
    size_t start = 0;

    if (has_bom && len >= 2) {
        if (data[0] == 0xFF && data[1] == 0xFE)
            big_endian = 0;
        else if (data[0] == 0xFE && data[1] == 0xFF)
            big_endian = 1;
        start = 2;
    }

    /* Allocate worst case: each UTF-16 unit -> 3 UTF-8 bytes,
       surrogate pair -> 4 UTF-8 bytes */
    size_t max_chars = (len - start) / 2;
    char *out = malloc(max_chars * 4 + 1);
    if (!out) return NULL;

    size_t j = 0;
    for (size_t i = start; i + 1 < len; i += 2) {
        uint16_t cp;
        if (big_endian)
            cp = ((uint16_t)data[i] << 8) | data[i + 1];
        else
            cp = ((uint16_t)data[i + 1] << 8) | data[i];

        /* NUL terminator */
        if (cp == 0) break;

        /* Surrogate pair */
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < len) {
            uint16_t lo;
            if (big_endian)
                lo = ((uint16_t)data[i + 2] << 8) | data[i + 3];
            else
                lo = ((uint16_t)data[i + 3] << 8) | data[i + 2];

            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                uint32_t full = 0x10000 + ((uint32_t)(cp - 0xD800) << 10) +
                                (lo - 0xDC00);
                out[j++] = (char)(0xF0 | ((full >> 18) & 0x07));
                out[j++] = (char)(0x80 | ((full >> 12) & 0x3F));
                out[j++] = (char)(0x80 | ((full >> 6)  & 0x3F));
                out[j++] = (char)(0x80 | (full & 0x3F));
                i += 2;  /* skip low surrogate */
                continue;
            }
        }

        /* BMP character */
        if (cp < 0x80) {
            out[j++] = (char)cp;
        } else if (cp < 0x800) {
            out[j++] = (char)(0xC0 | (cp >> 6));
            out[j++] = (char)(0x80 | (cp & 0x3F));
        } else {
            out[j++] = (char)(0xE0 | (cp >> 12));
            out[j++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[j++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[j] = '\0';
    return out;
}

static char *decode_text(uint8_t encoding, const uint8_t *data, size_t len)
{
    switch (encoding) {
    case ID3V2_ENC_ISO8859_1:
        return decode_iso8859_1(data, len);
    case ID3V2_ENC_UTF16_BOM:
        return decode_utf16(data, len, 1, 0);
    case ID3V2_ENC_UTF16BE:
        return decode_utf16(data, len, 0, 1);
    case ID3V2_ENC_UTF8:
        return decode_utf8(data, len);
    default:
        return decode_iso8859_1(data, len);
    }
}

/*
 * Find the NUL terminator for a given encoding.
 * ISO-8859-1 and UTF-8 use a single 0x00 byte.
 * UTF-16 variants use 0x00 0x00.
 * Returns the offset of the terminator, or `len` if not found.
 */
static size_t find_text_terminator(uint8_t encoding,
                                   const uint8_t *data, size_t len)
{
    if (encoding == ID3V2_ENC_UTF16_BOM || encoding == ID3V2_ENC_UTF16BE) {
        for (size_t i = 0; i + 1 < len; i += 2) {
            if (data[i] == 0 && data[i + 1] == 0)
                return i;
        }
        return len;
    }
    for (size_t i = 0; i < len; i++) {
        if (data[i] == 0) return i;
    }
    return len;
}

static size_t terminator_size(uint8_t encoding)
{
    return (encoding == ID3V2_ENC_UTF16_BOM ||
            encoding == ID3V2_ENC_UTF16BE) ? 2 : 1;
}

/* ------------------------------------------------------------------ */
/*  Frame parsing                                                      */
/* ------------------------------------------------------------------ */

int id3v2_read_frames(file_handle_t *fh, int64_t base_offset,
                      const id3v2_header_t *hdr, id3v2_frame_t **frames)
{
    if (!fh || !hdr || !frames)
        return MP3TAG_ERR_INVALID_ARG;

    *frames = NULL;
    id3v2_frame_t *tail = NULL;

    int64_t tag_start = base_offset + ID3V2_HEADER_SIZE;
    int64_t tag_end   = tag_start + (int64_t)hdr->tag_size;

    /* Skip extended header if present */
    if (hdr->flags & ID3V2_FLAG_EXTENDED) {
        uint8_t ext_buf[4];
        if (file_seek(fh, tag_start) != 0)
            return MP3TAG_ERR_SEEK_FAILED;
        if (file_read(fh, ext_buf, 4) != 0)
            return MP3TAG_ERR_TRUNCATED;

        uint32_t ext_size;
        if (hdr->version_major == 4)
            ext_size = id3v2_syncsafe_decode(ext_buf);
        else
            ext_size = id3v2_be32_decode(ext_buf);

        /* v2.4: ext_size includes itself; v2.3: ext_size excludes the 4 bytes */
        if (hdr->version_major == 4)
            tag_start += ext_size;
        else
            tag_start += 4 + ext_size;
    }

    int64_t pos = tag_start;

    while (pos + ID3V2_FRAME_HEADER_SIZE <= tag_end) {
        uint8_t fhdr[ID3V2_FRAME_HEADER_SIZE];
        if (file_seek(fh, pos) != 0)
            break;
        if (file_read(fh, fhdr, ID3V2_FRAME_HEADER_SIZE) != 0)
            break;

        /* Check for padding (all zeros = end of frames) */
        if (fhdr[0] == 0)
            break;

        /* Validate frame ID: must be uppercase A-Z or 0-9 */
        int valid_id = 1;
        for (int i = 0; i < 4; i++) {
            if (!((fhdr[i] >= 'A' && fhdr[i] <= 'Z') ||
                  (fhdr[i] >= '0' && fhdr[i] <= '9'))) {
                valid_id = 0;
                break;
            }
        }
        if (!valid_id)
            break;

        /* Decode frame size */
        uint32_t frame_size;
        if (hdr->version_major == 4)
            frame_size = id3v2_syncsafe_decode(fhdr + 4);
        else
            frame_size = id3v2_be32_decode(fhdr + 4);

        uint16_t frame_flags = ((uint16_t)fhdr[8] << 8) | fhdr[9];

        /* Sanity check */
        if (pos + ID3V2_FRAME_HEADER_SIZE + frame_size > tag_end)
            break;

        /* Read frame data */
        uint8_t *data = malloc(frame_size);
        if (!data) {
            id3v2_free_frames(*frames);
            *frames = NULL;
            return MP3TAG_ERR_NO_MEMORY;
        }

        if (file_read(fh, data, frame_size) != 0) {
            free(data);
            id3v2_free_frames(*frames);
            *frames = NULL;
            return MP3TAG_ERR_TRUNCATED;
        }

        /* Create frame node */
        id3v2_frame_t *frame = calloc(1, sizeof(*frame));
        if (!frame) {
            free(data);
            id3v2_free_frames(*frames);
            *frames = NULL;
            return MP3TAG_ERR_NO_MEMORY;
        }

        memcpy(frame->id, fhdr, 4);
        frame->id[4]    = '\0';
        frame->data      = data;
        frame->data_size = frame_size;
        frame->flags     = frame_flags;

        /* Append to list */
        if (!*frames) {
            *frames = frame;
        } else {
            tail->next = frame;
        }
        tail = frame;

        pos += ID3V2_FRAME_HEADER_SIZE + frame_size;
    }

    return MP3TAG_OK;
}

/* ------------------------------------------------------------------ */
/*  Frame-to-collection conversion                                     */
/* ------------------------------------------------------------------ */

static mp3tag_simple_tag_t *make_simple_tag(const char *name, const char *value)
{
    mp3tag_simple_tag_t *st = calloc(1, sizeof(*st));
    if (!st) return NULL;
    st->name  = str_dup(name);
    st->value = str_dup(value);
    if (!st->name || !st->value) {
        free(st->name);
        free(st->value);
        free(st);
        return NULL;
    }
    return st;
}

static mp3tag_simple_tag_t *make_binary_tag(const char *name,
                                            const uint8_t *data, size_t size)
{
    mp3tag_simple_tag_t *st = calloc(1, sizeof(*st));
    if (!st) return NULL;
    st->name = str_dup(name);
    if (!st->name) { free(st); return NULL; }
    st->binary = malloc(size);
    if (!st->binary) { free(st->name); free(st); return NULL; }
    memcpy(st->binary, data, size);
    st->binary_size = size;
    return st;
}

static void append_simple_tag(mp3tag_tag_t *tag, mp3tag_simple_tag_t *st)
{
    if (!st) return;
    if (!tag->simple_tags) {
        tag->simple_tags = st;
    } else {
        mp3tag_simple_tag_t *tail = tag->simple_tags;
        while (tail->next) tail = tail->next;
        tail->next = st;
    }
}

static void parse_text_frame(const id3v2_frame_t *frame, mp3tag_tag_t *tag)
{
    if (frame->data_size < 1) return;

    uint8_t encoding = frame->data[0];
    char *text = decode_text(encoding, frame->data + 1, frame->data_size - 1);
    if (!text) return;

    /* Map frame ID to human-readable name */
    const char *name = id3v2_frame_id_to_name(frame->id);
    mp3tag_simple_tag_t *st;

    if (name) {
        st = make_simple_tag(name, text);
    } else {
        /* Unknown text frame: use frame ID as name */
        st = make_simple_tag(frame->id, text);
    }
    free(text);
    append_simple_tag(tag, st);
}

static void parse_txxx_frame(const id3v2_frame_t *frame, mp3tag_tag_t *tag)
{
    if (frame->data_size < 2) return;

    uint8_t encoding = frame->data[0];
    const uint8_t *rest = frame->data + 1;
    size_t rest_len = frame->data_size - 1;

    /* Find NUL separator between description and value */
    size_t desc_end = find_text_terminator(encoding, rest, rest_len);
    char *desc = decode_text(encoding, rest, desc_end);
    if (!desc) return;

    size_t tsz = terminator_size(encoding);
    size_t val_start = desc_end + tsz;
    char *value = NULL;

    if (val_start < rest_len) {
        value = decode_text(encoding, rest + val_start, rest_len - val_start);
    } else {
        value = str_dup("");
    }

    if (!value) { free(desc); return; }

    /* Use description as the tag name */
    mp3tag_simple_tag_t *st = make_simple_tag(desc, value);
    free(desc);
    free(value);
    append_simple_tag(tag, st);
}

static void parse_comm_frame(const id3v2_frame_t *frame, mp3tag_tag_t *tag)
{
    /* COMM: encoding(1) + language(3) + short_description(NUL) + text */
    if (frame->data_size < 5) return;

    uint8_t encoding = frame->data[0];
    char lang[4] = {
        (char)frame->data[1],
        (char)frame->data[2],
        (char)frame->data[3],
        '\0'
    };

    const uint8_t *rest = frame->data + 4;
    size_t rest_len = frame->data_size - 4;

    /* Skip short description */
    size_t desc_end = find_text_terminator(encoding, rest, rest_len);
    size_t tsz = terminator_size(encoding);
    size_t val_start = desc_end + tsz;

    char *text = NULL;
    if (val_start < rest_len) {
        text = decode_text(encoding, rest + val_start, rest_len - val_start);
    } else {
        text = str_dup("");
    }
    if (!text) return;

    mp3tag_simple_tag_t *st = make_simple_tag("COMMENT", text);
    free(text);
    if (st && lang[0] != '\0') {
        st->language = str_dup(lang);
    }
    append_simple_tag(tag, st);
}

static void parse_binary_frame(const id3v2_frame_t *frame, mp3tag_tag_t *tag)
{
    const char *name = id3v2_frame_id_to_name(frame->id);
    mp3tag_simple_tag_t *st;

    if (name) {
        st = make_binary_tag(name, frame->data, frame->data_size);
    } else {
        st = make_binary_tag(frame->id, frame->data, frame->data_size);
    }
    append_simple_tag(tag, st);
}

int id3v2_frames_to_collection(const id3v2_frame_t *frames,
                               mp3tag_collection_t **coll)
{
    if (!coll) return MP3TAG_ERR_INVALID_ARG;

    mp3tag_collection_t *c = calloc(1, sizeof(*c));
    if (!c) return MP3TAG_ERR_NO_MEMORY;

    mp3tag_tag_t *tag = calloc(1, sizeof(*tag));
    if (!tag) { free(c); return MP3TAG_ERR_NO_MEMORY; }

    tag->target_type = MP3TAG_TARGET_ALBUM;
    c->tags  = tag;
    c->count = 1;

    for (const id3v2_frame_t *f = frames; f; f = f->next) {
        /* Skip frames with compression/encryption (unsupported) */
        if (f->flags & (ID3V2_FRAME_FLAG_COMPRESS | ID3V2_FRAME_FLAG_ENCRYPT))
            continue;

        if (f->id[0] == 'T' && f->id[1] == 'X' &&
            f->id[2] == 'X' && f->id[3] == 'X') {
            parse_txxx_frame(f, tag);
        } else if (f->id[0] == 'T') {
            parse_text_frame(f, tag);
        } else if (f->id[0] == 'C' && f->id[1] == 'O' &&
                   f->id[2] == 'M' && f->id[3] == 'M') {
            parse_comm_frame(f, tag);
        } else {
            /* Non-text frame: store as binary */
            parse_binary_frame(f, tag);
        }
    }

    *coll = c;
    return MP3TAG_OK;
}

/* ------------------------------------------------------------------ */
/*  Cleanup                                                            */
/* ------------------------------------------------------------------ */

void id3v2_free_frames(id3v2_frame_t *frames)
{
    while (frames) {
        id3v2_frame_t *next = frames->next;
        free(frames->data);
        free(frames);
        frames = next;
    }
}
