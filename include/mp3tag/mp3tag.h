/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#ifndef MP3TAG_H
#define MP3TAG_H

#include "mp3tag_types.h"
#include "mp3tag_error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Version ---------- */

const char *mp3tag_version(void);

/* ---------- Error ---------- */

const char *mp3tag_strerror(int error);

/* ---------- Context lifecycle ---------- */

mp3tag_context_t *mp3tag_create(const mp3tag_allocator_t *allocator);
void              mp3tag_destroy(mp3tag_context_t *ctx);

int  mp3tag_open(mp3tag_context_t *ctx, const char *path);
int  mp3tag_open_rw(mp3tag_context_t *ctx, const char *path);
void mp3tag_close(mp3tag_context_t *ctx);
int  mp3tag_is_open(const mp3tag_context_t *ctx);

/* ---------- Tag reading ---------- */

/*
 * Read all tags from the file. The returned collection is owned by
 * the context and must not be freed by the caller. It remains valid
 * until the next call to mp3tag_read_tags, mp3tag_write_tags,
 * mp3tag_set_tag_string, mp3tag_remove_tag, or mp3tag_close.
 */
int mp3tag_read_tags(mp3tag_context_t *ctx, mp3tag_collection_t **tags);

/*
 * Read a single tag value by name (case-insensitive).
 * Copies the value into the caller-provided buffer.
 */
int mp3tag_read_tag_string(mp3tag_context_t *ctx, const char *name,
                           char *value, size_t size);

/* ---------- Tag writing ---------- */

/*
 * Replace all tags in the file with the given collection.
 * Attempts in-place write first; falls back to rewrite if needed.
 */
int mp3tag_write_tags(mp3tag_context_t *ctx, const mp3tag_collection_t *tags);

/*
 * Set or create a single tag. Pass NULL as value to remove the tag.
 * Tags are placed at the ALBUM target level (type 50).
 */
int mp3tag_set_tag_string(mp3tag_context_t *ctx, const char *name,
                          const char *value);

/*
 * Remove a tag by name.
 */
int mp3tag_remove_tag(mp3tag_context_t *ctx, const char *name);

/* ---------- Collection building ---------- */

mp3tag_collection_t *mp3tag_collection_create(mp3tag_context_t *ctx);
void                 mp3tag_collection_free(mp3tag_context_t *ctx,
                                            mp3tag_collection_t *coll);

mp3tag_tag_t *mp3tag_collection_add_tag(mp3tag_context_t *ctx,
                                        mp3tag_collection_t *coll,
                                        mp3tag_target_type_t type);

mp3tag_simple_tag_t *mp3tag_tag_add_simple(mp3tag_context_t *ctx,
                                           mp3tag_tag_t *tag,
                                           const char *name,
                                           const char *value);

mp3tag_simple_tag_t *mp3tag_simple_tag_add_nested(mp3tag_context_t *ctx,
                                                  mp3tag_simple_tag_t *parent,
                                                  const char *name,
                                                  const char *value);

int mp3tag_simple_tag_set_language(mp3tag_context_t *ctx,
                                  mp3tag_simple_tag_t *simple_tag,
                                  const char *language);

int mp3tag_tag_add_track_uid(mp3tag_context_t *ctx, mp3tag_tag_t *tag,
                             uint64_t uid);

#ifdef __cplusplus
}
#endif

#endif /* MP3TAG_H */
