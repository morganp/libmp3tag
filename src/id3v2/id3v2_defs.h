/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#ifndef ID3V2_DEFS_H
#define ID3V2_DEFS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- ID3v2 header constants ---------- */

#define ID3V2_HEADER_SIZE       10
#define ID3V2_FOOTER_SIZE       10

/* Header flags (byte 5) */
#define ID3V2_FLAG_UNSYNC       0x80
#define ID3V2_FLAG_EXTENDED     0x40
#define ID3V2_FLAG_EXPERIMENTAL 0x20
#define ID3V2_FLAG_FOOTER       0x10  /* v2.4 only */

/* Frame header sizes */
#define ID3V2_FRAME_HEADER_SIZE 10

/* Frame flags (bytes 8-9 of frame header) */
#define ID3V2_FRAME_FLAG_TAG_ALTER   0x4000
#define ID3V2_FRAME_FLAG_FILE_ALTER  0x2000
#define ID3V2_FRAME_FLAG_READ_ONLY   0x1000
#define ID3V2_FRAME_FLAG_GROUPING    0x0040
#define ID3V2_FRAME_FLAG_COMPRESS    0x0008
#define ID3V2_FRAME_FLAG_ENCRYPT     0x0004
#define ID3V2_FRAME_FLAG_UNSYNC      0x0002
#define ID3V2_FRAME_FLAG_DATA_LEN    0x0001

/* Text encoding values */
#define ID3V2_ENC_ISO8859_1    0
#define ID3V2_ENC_UTF16_BOM    1
#define ID3V2_ENC_UTF16BE      2
#define ID3V2_ENC_UTF8         3

/* Default padding added when rewriting the file */
#define ID3V2_DEFAULT_PADDING  4096

/* ---------- Syncsafe integer helpers ---------- */

static inline uint32_t id3v2_syncsafe_decode(const uint8_t b[4])
{
    return ((uint32_t)b[0] << 21) |
           ((uint32_t)b[1] << 14) |
           ((uint32_t)b[2] << 7)  |
           (uint32_t)b[3];
}

static inline void id3v2_syncsafe_encode(uint32_t val, uint8_t b[4])
{
    b[0] = (uint8_t)((val >> 21) & 0x7F);
    b[1] = (uint8_t)((val >> 14) & 0x7F);
    b[2] = (uint8_t)((val >> 7)  & 0x7F);
    b[3] = (uint8_t)(val & 0x7F);
}

static inline uint32_t id3v2_be32_decode(const uint8_t b[4])
{
    return ((uint32_t)b[0] << 24) |
           ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |
           (uint32_t)b[3];
}

static inline void id3v2_be32_encode(uint32_t val, uint8_t b[4])
{
    b[0] = (uint8_t)((val >> 24) & 0xFF);
    b[1] = (uint8_t)((val >> 16) & 0xFF);
    b[2] = (uint8_t)((val >> 8)  & 0xFF);
    b[3] = (uint8_t)(val & 0xFF);
}

/* ---------- Frame-ID-to-name mapping ---------- */

typedef struct {
    const char *name;       /* Human-readable tag name */
    const char *frame_id;   /* ID3v2.4 frame ID (4 chars) */
    const char *v23_id;     /* ID3v2.3 equivalent (NULL if same) */
} id3v2_name_map_t;

/*
 * Mapping table — terminated by {NULL, NULL, NULL}.
 * The name field matches the tag names used in libmkvtag where possible.
 */
static const id3v2_name_map_t id3v2_name_map[] = {
    {"TITLE",           "TIT2", NULL},
    {"SUBTITLE",        "TIT3", NULL},
    {"ARTIST",          "TPE1", NULL},
    {"ALBUM_ARTIST",    "TPE2", NULL},
    {"ALBUM",           "TALB", NULL},
    {"DATE_RELEASED",   "TDRC", "TYER"},
    {"TRACK_NUMBER",    "TRCK", NULL},
    {"DISC_NUMBER",     "TPOS", NULL},
    {"GENRE",           "TCON", NULL},
    {"COMPOSER",        "TCOM", NULL},
    {"LYRICIST",        "TEXT", NULL},
    {"CONDUCTOR",       "TPE3", NULL},
    {"COMMENT",         "COMM", NULL},
    {"ENCODER",         "TSSE", NULL},
    {"ENCODED_BY",      "TENC", NULL},
    {"COPYRIGHT",       "TCOP", NULL},
    {"BPM",             "TBPM", NULL},
    {"PUBLISHER",       "TPUB", NULL},
    {"ISRC",            "TSRC", NULL},
    {"GROUPING",        "TIT1", NULL},
    {"SORT_TITLE",      "TSOT", NULL},
    {"SORT_ARTIST",     "TSOP", NULL},
    {"SORT_ALBUM",      "TSOA", NULL},
    {"SORT_ALBUM_ARTIST","TSO2", NULL},
    {"ORIGINAL_DATE",   "TDOR", "TORY"},
    {NULL, NULL, NULL}
};

/*
 * Look up a human-readable name for a frame ID.
 * Returns the frame ID itself if no mapping is found.
 */
static inline const char *id3v2_frame_id_to_name(const char *frame_id)
{
    for (const id3v2_name_map_t *m = id3v2_name_map; m->name; m++) {
        if (frame_id[0] == m->frame_id[0] && frame_id[1] == m->frame_id[1] &&
            frame_id[2] == m->frame_id[2] && frame_id[3] == m->frame_id[3])
            return m->name;
        if (m->v23_id &&
            frame_id[0] == m->v23_id[0] && frame_id[1] == m->v23_id[1] &&
            frame_id[2] == m->v23_id[2] && frame_id[3] == m->v23_id[3])
            return m->name;
    }
    return NULL;  /* caller should use frame_id directly */
}

/*
 * Look up a frame ID for a human-readable tag name.
 * Returns NULL if no mapping is found (caller should try TXXX).
 */
static inline const char *id3v2_name_to_frame_id(const char *name)
{
    for (const id3v2_name_map_t *m = id3v2_name_map; m->name; m++) {
        /* Case-insensitive comparison (ASCII only) */
        const char *a = name;
        const char *b = m->name;
        while (*a && *b) {
            char ca = *a, cb = *b;
            if (ca >= 'a' && ca <= 'z') ca -= 32;
            if (cb >= 'a' && cb <= 'z') cb -= 32;
            if (ca != cb) break;
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0')
            return m->frame_id;
    }
    return NULL;
}

/*
 * Check if a frame ID is a text frame (starts with 'T' but not "TXXX").
 */
static inline int id3v2_is_text_frame(const char *frame_id)
{
    return frame_id[0] == 'T' &&
           !(frame_id[0] == 'T' && frame_id[1] == 'X' &&
             frame_id[2] == 'X' && frame_id[3] == 'X');
}

#ifdef __cplusplus
}
#endif

#endif /* ID3V2_DEFS_H */
