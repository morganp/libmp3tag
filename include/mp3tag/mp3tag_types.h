/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#ifndef MP3TAG_TYPES_H
#define MP3TAG_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Target type values — kept compatible with libmkvtag.
 * For MP3 files, ALBUM (50) is the default scope for all tags.
 */
typedef enum {
    MP3TAG_TARGET_COLLECTION = 70,
    MP3TAG_TARGET_EDITION    = 60,
    MP3TAG_TARGET_ALBUM      = 50,
    MP3TAG_TARGET_PART       = 40,
    MP3TAG_TARGET_TRACK      = 30,
    MP3TAG_TARGET_SUBTRACK   = 20,
    MP3TAG_TARGET_SHOT       = 10
} mp3tag_target_type_t;

/*
 * A name/value tag pair. Forms a singly-linked list.
 * Names use human-readable identifiers (e.g. "TITLE", "ARTIST").
 */
typedef struct mp3tag_simple_tag {
    char    *name;          /* Tag name (UTF-8) */
    char    *value;         /* String value (UTF-8, may be NULL) */
    uint8_t *binary;        /* Binary value (may be NULL) */
    size_t   binary_size;   /* Size of binary data */
    char    *language;      /* Language code (may be NULL, defaults to "und") */
    int      is_default;    /* Whether this is the default for the language */

    struct mp3tag_simple_tag *nested;  /* First nested child */
    struct mp3tag_simple_tag *next;    /* Next sibling */
} mp3tag_simple_tag_t;

/*
 * A tag with a target specification and a list of simple tags.
 * For MP3 files, target_type is typically MP3TAG_TARGET_ALBUM.
 */
typedef struct mp3tag_tag {
    mp3tag_target_type_t  target_type;
    char                 *target_type_str;

    uint64_t *track_uids;
    size_t    track_uid_count;
    uint64_t *edition_uids;
    size_t    edition_uid_count;
    uint64_t *chapter_uids;
    size_t    chapter_uid_count;
    uint64_t *attachment_uids;
    size_t    attachment_uid_count;

    mp3tag_simple_tag_t  *simple_tags;
    struct mp3tag_tag    *next;
} mp3tag_tag_t;

/*
 * A collection of tags.
 */
typedef struct {
    mp3tag_tag_t *tags;
    size_t        count;
} mp3tag_collection_t;

/*
 * Custom allocator interface.
 */
typedef struct {
    void *(*alloc)(size_t size, void *user_data);
    void *(*realloc)(void *ptr, size_t size, void *user_data);
    void  (*free)(void *ptr, void *user_data);
    void  *user_data;
} mp3tag_allocator_t;

/*
 * Opaque context — all operations go through this.
 */
typedef struct mp3tag_context mp3tag_context_t;

#ifdef __cplusplus
}
#endif

#endif /* MP3TAG_TYPES_H */
