/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#include "id3v1.h"
#include "../../include/mp3tag/mp3tag_error.h"
#include "../util/string_util.h"

#include <stdlib.h>
#include <string.h>

int id3v1_detect(file_handle_t *fh)
{
    int64_t fsize = file_size(fh);
    if (fsize < ID3V1_TAG_SIZE)
        return 0;

    uint8_t header[3];
    if (file_seek(fh, fsize - ID3V1_TAG_SIZE) != 0)
        return MP3TAG_ERR_SEEK_FAILED;
    if (file_read(fh, header, 3) != 0)
        return MP3TAG_ERR_IO;

    return (header[0] == 'T' && header[1] == 'A' && header[2] == 'G') ? 1 : 0;
}

/*
 * ID3v1 layout (128 bytes):
 *   0-2:   "TAG"
 *   3-32:  Title  (30 bytes)
 *   33-62: Artist (30 bytes)
 *   63-92: Album  (30 bytes)
 *   93-96: Year   (4 bytes, ASCII)
 *   97-126: Comment (30 bytes; if byte 125=0 and byte 126!=0, it's ID3v1.1
 *           with track number in byte 126)
 *   127:   Genre  (1 byte, index)
 */

static mp3tag_simple_tag_t *add_simple(mp3tag_tag_t *tag,
                                       const char *name, const char *value)
{
    if (!value || value[0] == '\0')
        return NULL;

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

    /* Append to tail of simple_tags list */
    if (!tag->simple_tags) {
        tag->simple_tags = st;
    } else {
        mp3tag_simple_tag_t *tail = tag->simple_tags;
        while (tail->next) tail = tail->next;
        tail->next = st;
    }
    return st;
}

int id3v1_read(file_handle_t *fh, mp3tag_collection_t **coll)
{
    if (!fh || !coll) return MP3TAG_ERR_INVALID_ARG;

    int detected = id3v1_detect(fh);
    if (detected <= 0)
        return detected == 0 ? MP3TAG_ERR_NO_TAGS : detected;

    /* Read the full 128-byte tag */
    int64_t fsize = file_size(fh);
    uint8_t raw[ID3V1_TAG_SIZE];

    if (file_seek(fh, fsize - ID3V1_TAG_SIZE) != 0)
        return MP3TAG_ERR_SEEK_FAILED;
    if (file_read(fh, raw, ID3V1_TAG_SIZE) != 0)
        return MP3TAG_ERR_IO;

    /* Parse fixed-width fields */
    char title[31], artist[31], album[31], year[5], comment[31];
    str_trim_fixed(title,   (const char *)raw + 3,  30);
    str_trim_fixed(artist,  (const char *)raw + 33, 30);
    str_trim_fixed(album,   (const char *)raw + 63, 30);
    str_trim_fixed(year,    (const char *)raw + 93,  4);
    str_trim_fixed(comment, (const char *)raw + 97, 30);

    /* ID3v1.1: track number */
    char track[4] = {0};
    if (raw[125] == 0 && raw[126] != 0) {
        int tnum = raw[126];
        /* Manual int-to-string for 1-255 */
        if (tnum >= 100) {
            track[0] = '0' + (tnum / 100);
            track[1] = '0' + ((tnum / 10) % 10);
            track[2] = '0' + (tnum % 10);
        } else if (tnum >= 10) {
            track[0] = '0' + (tnum / 10);
            track[1] = '0' + (tnum % 10);
        } else {
            track[0] = '0' + tnum;
        }
        /* Truncate comment to 28 chars if we have a track number */
        comment[28] = '\0';
        /* Re-trim */
        size_t end = strlen(comment);
        while (end > 0 && (comment[end - 1] == ' ' || comment[end - 1] == '\0'))
            end--;
        comment[end] = '\0';
    }

    /* Build collection */
    mp3tag_collection_t *c = calloc(1, sizeof(*c));
    if (!c) return MP3TAG_ERR_NO_MEMORY;

    mp3tag_tag_t *tag = calloc(1, sizeof(*tag));
    if (!tag) { free(c); return MP3TAG_ERR_NO_MEMORY; }

    tag->target_type = MP3TAG_TARGET_ALBUM;
    c->tags  = tag;
    c->count = 1;

    add_simple(tag, "TITLE",         title);
    add_simple(tag, "ARTIST",        artist);
    add_simple(tag, "ALBUM",         album);
    add_simple(tag, "DATE_RELEASED", year);
    add_simple(tag, "COMMENT",       comment);
    add_simple(tag, "TRACK_NUMBER",  track);

    /* Genre byte — just store as the numeric index string */
    if (raw[127] != 0xFF) {
        char genre_str[4];
        int g = raw[127];
        if (g >= 100) {
            genre_str[0] = '0' + (g / 100);
            genre_str[1] = '0' + ((g / 10) % 10);
            genre_str[2] = '0' + (g % 10);
            genre_str[3] = '\0';
        } else if (g >= 10) {
            genre_str[0] = '0' + (g / 10);
            genre_str[1] = '0' + (g % 10);
            genre_str[2] = '\0';
        } else {
            genre_str[0] = '0' + g;
            genre_str[1] = '\0';
        }
        add_simple(tag, "GENRE", genre_str);
    }

    *coll = c;
    return MP3TAG_OK;
}
