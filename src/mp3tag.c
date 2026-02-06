/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#include "../include/mp3tag/mp3tag.h"
#include "id3v2/id3v2_reader.h"
#include "id3v2/id3v2_writer.h"
#include "id3v2/id3v2_defs.h"
#include "id3v1/id3v1.h"
#include "io/file_io.h"
#include "util/buffer.h"
#include "util/string_util.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/*  Internal context definition                                        */
/* ------------------------------------------------------------------ */

struct mp3tag_context {
    mp3tag_allocator_t  allocator;
    int                 has_allocator;

    file_handle_t      *fh;
    char               *path;
    int                 writable;

    /* Parsed file structure */
    int                 has_id3v2;
    id3v2_header_t      id3v2_hdr;
    int64_t             audio_offset;  /* First byte of audio data */

    int                 has_id3v1;

    /* Cached tag collection (owned by context) */
    mp3tag_collection_t *cached_tags;
};

/* ------------------------------------------------------------------ */
/*  Collection / tag freeing                                           */
/* ------------------------------------------------------------------ */

static void free_simple_tags(mp3tag_simple_tag_t *st)
{
    while (st) {
        mp3tag_simple_tag_t *next = st->next;
        free(st->name);
        free(st->value);
        free(st->binary);
        free(st->language);
        free_simple_tags(st->nested);
        free(st);
        st = next;
    }
}

static void free_tag(mp3tag_tag_t *tag)
{
    while (tag) {
        mp3tag_tag_t *next = tag->next;
        free(tag->target_type_str);
        free(tag->track_uids);
        free(tag->edition_uids);
        free(tag->chapter_uids);
        free(tag->attachment_uids);
        free_simple_tags(tag->simple_tags);
        free(tag);
        tag = next;
    }
}

static void free_collection(mp3tag_collection_t *coll)
{
    if (!coll) return;
    free_tag(coll->tags);
    free(coll);
}

static void invalidate_cache(mp3tag_context_t *ctx)
{
    if (ctx->cached_tags) {
        free_collection(ctx->cached_tags);
        ctx->cached_tags = NULL;
    }
}

/* ------------------------------------------------------------------ */
/*  Version / Error                                                    */
/* ------------------------------------------------------------------ */

const char *mp3tag_version(void)
{
    return "1.0.0";
}

const char *mp3tag_strerror(int error)
{
    switch (error) {
    case MP3TAG_OK:                return "Success";
    case MP3TAG_ERR_INVALID_ARG:   return "Invalid argument";
    case MP3TAG_ERR_NO_MEMORY:     return "Out of memory";
    case MP3TAG_ERR_IO:            return "I/O error";
    case MP3TAG_ERR_NOT_OPEN:      return "File not open";
    case MP3TAG_ERR_ALREADY_OPEN:  return "File already open";
    case MP3TAG_ERR_READ_ONLY:     return "File opened read-only";
    case MP3TAG_ERR_NOT_MP3:       return "Not an MP3 file or no ID3 tag";
    case MP3TAG_ERR_BAD_ID3V2:     return "Invalid ID3v2 header";
    case MP3TAG_ERR_CORRUPT:       return "File is corrupted";
    case MP3TAG_ERR_TRUNCATED:     return "Unexpected end of file";
    case MP3TAG_ERR_UNSUPPORTED:   return "Unsupported ID3v2 version";
    case MP3TAG_ERR_NO_TAGS:       return "No tags found";
    case MP3TAG_ERR_TAG_NOT_FOUND: return "Tag not found";
    case MP3TAG_ERR_TAG_TOO_LARGE: return "Tag data too large for buffer";
    case MP3TAG_ERR_NO_SPACE:      return "Not enough space for in-place write";
    case MP3TAG_ERR_WRITE_FAILED:  return "Write operation failed";
    case MP3TAG_ERR_SEEK_FAILED:   return "Seek operation failed";
    case MP3TAG_ERR_RENAME_FAILED: return "File rename failed";
    default:                       return "Unknown error";
    }
}

/* ------------------------------------------------------------------ */
/*  Context lifecycle                                                  */
/* ------------------------------------------------------------------ */

mp3tag_context_t *mp3tag_create(const mp3tag_allocator_t *allocator)
{
    mp3tag_context_t *ctx;

    if (allocator && allocator->alloc) {
        ctx = allocator->alloc(sizeof(*ctx), allocator->user_data);
        if (!ctx) return NULL;
        memset(ctx, 0, sizeof(*ctx));
        ctx->allocator     = *allocator;
        ctx->has_allocator = 1;
    } else {
        ctx = calloc(1, sizeof(*ctx));
    }

    return ctx;
}

void mp3tag_destroy(mp3tag_context_t *ctx)
{
    if (!ctx) return;
    mp3tag_close(ctx);

    if (ctx->has_allocator && ctx->allocator.free)
        ctx->allocator.free(ctx, ctx->allocator.user_data);
    else
        free(ctx);
}

static int probe_file(mp3tag_context_t *ctx)
{
    /* Try ID3v2 header */
    int rc = id3v2_read_header(ctx->fh, &ctx->id3v2_hdr);
    if (rc == MP3TAG_OK) {
        ctx->has_id3v2 = 1;
        /* Audio starts after the ID3v2 tag */
        ctx->audio_offset = ID3V2_HEADER_SIZE + ctx->id3v2_hdr.tag_size;
        if (ctx->id3v2_hdr.has_footer)
            ctx->audio_offset += ID3V2_FOOTER_SIZE;
    } else {
        ctx->has_id3v2    = 0;
        ctx->audio_offset = 0;
    }

    /* Check for ID3v1 at end of file */
    int v1 = id3v1_detect(ctx->fh);
    ctx->has_id3v1 = (v1 == 1);

    return MP3TAG_OK;
}

int mp3tag_open(mp3tag_context_t *ctx, const char *path)
{
    if (!ctx || !path)           return MP3TAG_ERR_INVALID_ARG;
    if (ctx->fh)                 return MP3TAG_ERR_ALREADY_OPEN;

    ctx->fh = file_open_read(path);
    if (!ctx->fh)                return MP3TAG_ERR_IO;

    ctx->path     = str_dup(path);
    ctx->writable = 0;

    return probe_file(ctx);
}

int mp3tag_open_rw(mp3tag_context_t *ctx, const char *path)
{
    if (!ctx || !path)           return MP3TAG_ERR_INVALID_ARG;
    if (ctx->fh)                 return MP3TAG_ERR_ALREADY_OPEN;

    ctx->fh = file_open_rw(path);
    if (!ctx->fh)                return MP3TAG_ERR_IO;

    ctx->path     = str_dup(path);
    ctx->writable = 1;

    return probe_file(ctx);
}

void mp3tag_close(mp3tag_context_t *ctx)
{
    if (!ctx) return;
    invalidate_cache(ctx);
    if (ctx->fh) {
        file_close(ctx->fh);
        ctx->fh = NULL;
    }
    free(ctx->path);
    ctx->path       = NULL;
    ctx->writable   = 0;
    ctx->has_id3v2  = 0;
    ctx->has_id3v1  = 0;
}

int mp3tag_is_open(const mp3tag_context_t *ctx)
{
    return (ctx && ctx->fh) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/*  Tag reading                                                        */
/* ------------------------------------------------------------------ */

int mp3tag_read_tags(mp3tag_context_t *ctx, mp3tag_collection_t **tags)
{
    if (!ctx || !tags)     return MP3TAG_ERR_INVALID_ARG;
    if (!ctx->fh)          return MP3TAG_ERR_NOT_OPEN;

    /* Return cached tags if available */
    if (ctx->cached_tags) {
        *tags = ctx->cached_tags;
        return MP3TAG_OK;
    }

    /* Try ID3v2 first */
    if (ctx->has_id3v2) {
        id3v2_frame_t *frames = NULL;
        int rc = id3v2_read_frames(ctx->fh, &ctx->id3v2_hdr, &frames);
        if (rc != MP3TAG_OK)
            return rc;

        mp3tag_collection_t *coll = NULL;
        rc = id3v2_frames_to_collection(frames, &coll);
        id3v2_free_frames(frames);
        if (rc != MP3TAG_OK)
            return rc;

        ctx->cached_tags = coll;
        *tags = coll;
        return MP3TAG_OK;
    }

    /* Fall back to ID3v1 */
    if (ctx->has_id3v1) {
        mp3tag_collection_t *coll = NULL;
        int rc = id3v1_read(ctx->fh, &coll);
        if (rc != MP3TAG_OK)
            return rc;

        ctx->cached_tags = coll;
        *tags = coll;
        return MP3TAG_OK;
    }

    return MP3TAG_ERR_NO_TAGS;
}

int mp3tag_read_tag_string(mp3tag_context_t *ctx, const char *name,
                           char *value, size_t size)
{
    if (!ctx || !name || !value || size == 0)
        return MP3TAG_ERR_INVALID_ARG;

    mp3tag_collection_t *coll = NULL;
    int rc = mp3tag_read_tags(ctx, &coll);
    if (rc != MP3TAG_OK) return rc;

    /* Search all tags and simple tags for a matching name */
    for (const mp3tag_tag_t *tag = coll->tags; tag; tag = tag->next) {
        for (const mp3tag_simple_tag_t *st = tag->simple_tags; st; st = st->next) {
            if (st->name && st->value && str_casecmp(st->name, name) == 0) {
                return str_copy(value, size, st->value) == 0
                       ? MP3TAG_OK : MP3TAG_ERR_TAG_TOO_LARGE;
            }
        }
    }

    return MP3TAG_ERR_TAG_NOT_FOUND;
}

/* ------------------------------------------------------------------ */
/*  Tag writing                                                        */
/* ------------------------------------------------------------------ */

/*
 * Try to write the new tag data in place.
 * Returns MP3TAG_OK on success, MP3TAG_ERR_NO_SPACE if it doesn't fit.
 */
static int try_write_inplace(mp3tag_context_t *ctx, dyn_buffer_t *frame_buf)
{
    if (!ctx->has_id3v2)
        return MP3TAG_ERR_NO_SPACE;

    uint32_t available = ctx->id3v2_hdr.tag_size;  /* bytes between header and audio */
    uint32_t needed    = (uint32_t)frame_buf->size;

    if (needed > available)
        return MP3TAG_ERR_NO_SPACE;

    /* Build new header with same total size */
    uint8_t hdr[ID3V2_HEADER_SIZE];
    id3v2_build_header(available, hdr);

    /* Write header */
    if (file_seek(ctx->fh, 0) != 0)
        return MP3TAG_ERR_SEEK_FAILED;
    if (file_write(ctx->fh, hdr, ID3V2_HEADER_SIZE) != 0)
        return MP3TAG_ERR_WRITE_FAILED;

    /* Write frames */
    if (file_write(ctx->fh, frame_buf->data, frame_buf->size) != 0)
        return MP3TAG_ERR_WRITE_FAILED;

    /* Zero-fill remaining space (padding) */
    uint32_t padding = available - needed;
    if (padding > 0) {
        uint8_t zeros[4096];
        memset(zeros, 0, sizeof(zeros));
        while (padding > 0) {
            uint32_t chunk = padding < sizeof(zeros) ? padding : (uint32_t)sizeof(zeros);
            if (file_write(ctx->fh, zeros, chunk) != 0)
                return MP3TAG_ERR_WRITE_FAILED;
            padding -= chunk;
        }
    }

    if (file_sync(ctx->fh) != 0)
        return MP3TAG_ERR_IO;

    return MP3TAG_OK;
}

/*
 * Rewrite the file with a new ID3v2 tag.
 * Uses a temp file to avoid loading the entire file into memory.
 */
static int rewrite_file(mp3tag_context_t *ctx, dyn_buffer_t *frame_buf)
{
    if (!ctx->path)
        return MP3TAG_ERR_INVALID_ARG;

    /* Build temp path */
    size_t path_len = strlen(ctx->path);
    char *tmp_path = malloc(path_len + 5);
    if (!tmp_path) return MP3TAG_ERR_NO_MEMORY;
    memcpy(tmp_path, ctx->path, path_len);
    memcpy(tmp_path + path_len, ".tmp", 5);

    /* Calculate new tag size with padding */
    uint32_t body_size = (uint32_t)frame_buf->size + ID3V2_DEFAULT_PADDING;

    /* Build header */
    uint8_t hdr[ID3V2_HEADER_SIZE];
    id3v2_build_header(body_size, hdr);

    /* Open temp file for writing */
    file_handle_t *tmp = file_open_rw(tmp_path);
    if (!tmp) {
        /* Try to create the file first */
        FILE *f = fopen(tmp_path, "wb");
        if (!f) { free(tmp_path); return MP3TAG_ERR_IO; }
        fclose(f);
        tmp = file_open_rw(tmp_path);
        if (!tmp) { free(tmp_path); return MP3TAG_ERR_IO; }
    }

    int result = MP3TAG_OK;

    /* Write new ID3v2 tag */
    if (file_seek(tmp, 0) != 0 ||
        file_write(tmp, hdr, ID3V2_HEADER_SIZE) != 0 ||
        file_write(tmp, frame_buf->data, frame_buf->size) != 0) {
        result = MP3TAG_ERR_WRITE_FAILED;
        goto cleanup;
    }

    /* Write padding zeros */
    {
        uint8_t zeros[4096];
        memset(zeros, 0, sizeof(zeros));
        uint32_t remaining = ID3V2_DEFAULT_PADDING;
        while (remaining > 0) {
            uint32_t chunk = remaining < sizeof(zeros) ? remaining : (uint32_t)sizeof(zeros);
            if (file_write(tmp, zeros, chunk) != 0) {
                result = MP3TAG_ERR_WRITE_FAILED;
                goto cleanup;
            }
            remaining -= chunk;
        }
    }

    /* Copy audio data from original file */
    {
        int64_t src_offset = ctx->audio_offset;
        int64_t src_end    = file_size(ctx->fh);
        if (ctx->has_id3v1)
            /* Include ID3v1 tag in the copy — we don't strip it */
            ;  /* src_end already includes it */

        if (file_seek(ctx->fh, src_offset) != 0) {
            result = MP3TAG_ERR_SEEK_FAILED;
            goto cleanup;
        }

        uint8_t copy_buf[65536];
        int64_t bytes_left = src_end - src_offset;

        while (bytes_left > 0) {
            size_t to_read = (size_t)(bytes_left < (int64_t)sizeof(copy_buf)
                                      ? bytes_left : (int64_t)sizeof(copy_buf));
            int64_t n = file_read_partial(ctx->fh, copy_buf, to_read);
            if (n <= 0) {
                result = (n == 0) ? MP3TAG_OK : MP3TAG_ERR_IO;
                break;
            }
            if (file_write(tmp, copy_buf, (size_t)n) != 0) {
                result = MP3TAG_ERR_WRITE_FAILED;
                goto cleanup;
            }
            bytes_left -= n;
        }
    }

    if (file_sync(tmp) != 0) {
        result = MP3TAG_ERR_IO;
        goto cleanup;
    }

    /* Close both files before rename */
    file_close(tmp);
    tmp = NULL;
    file_close(ctx->fh);
    ctx->fh = NULL;

    /* Atomic rename */
    if (rename(tmp_path, ctx->path) != 0) {
        result = MP3TAG_ERR_RENAME_FAILED;
        /* Try to reopen the original */
        if (ctx->writable)
            ctx->fh = file_open_rw(ctx->path);
        else
            ctx->fh = file_open_read(ctx->path);
        goto cleanup_path_only;
    }

    /* Reopen the file */
    if (ctx->writable)
        ctx->fh = file_open_rw(ctx->path);
    else
        ctx->fh = file_open_read(ctx->path);

    if (!ctx->fh) {
        result = MP3TAG_ERR_IO;
        goto cleanup_path_only;
    }

    /* Re-probe the file structure */
    probe_file(ctx);

cleanup:
    if (tmp) {
        file_close(tmp);
        unlink(tmp_path);
    }
cleanup_path_only:
    free(tmp_path);
    return result;
}

int mp3tag_write_tags(mp3tag_context_t *ctx, const mp3tag_collection_t *tags)
{
    if (!ctx || !tags)   return MP3TAG_ERR_INVALID_ARG;
    if (!ctx->fh)        return MP3TAG_ERR_NOT_OPEN;
    if (!ctx->writable)  return MP3TAG_ERR_READ_ONLY;

    /* Serialize frames */
    dyn_buffer_t frame_buf;
    buffer_init(&frame_buf);

    int rc = id3v2_serialize_frames(tags, &frame_buf);
    if (rc != MP3TAG_OK) {
        buffer_free(&frame_buf);
        return rc;
    }

    /* Invalidate cached tags */
    invalidate_cache(ctx);

    /* Strategy 1: try in-place */
    rc = try_write_inplace(ctx, &frame_buf);
    if (rc == MP3TAG_OK) {
        buffer_free(&frame_buf);
        /* Re-probe to update cached header info */
        probe_file(ctx);
        return MP3TAG_OK;
    }

    /* Strategy 2: rewrite the file */
    rc = rewrite_file(ctx, &frame_buf);
    buffer_free(&frame_buf);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  Convenience: set / remove single tag                               */
/* ------------------------------------------------------------------ */

/*
 * Clone a simple tag (shallow — does not clone nested or next).
 */
static mp3tag_simple_tag_t *clone_simple_tag(const mp3tag_simple_tag_t *src)
{
    mp3tag_simple_tag_t *st = calloc(1, sizeof(*st));
    if (!st) return NULL;

    st->name       = str_dup(src->name);
    st->value      = str_dup(src->value);
    st->language   = str_dup(src->language);
    st->is_default = src->is_default;

    if (src->binary && src->binary_size > 0) {
        st->binary = malloc(src->binary_size);
        if (st->binary) {
            memcpy(st->binary, src->binary, src->binary_size);
            st->binary_size = src->binary_size;
        }
    }

    return st;
}

int mp3tag_set_tag_string(mp3tag_context_t *ctx, const char *name,
                          const char *value)
{
    if (!ctx || !name)   return MP3TAG_ERR_INVALID_ARG;
    if (!ctx->fh)        return MP3TAG_ERR_NOT_OPEN;
    if (!ctx->writable)  return MP3TAG_ERR_READ_ONLY;

    /* Read existing tags (or start fresh) */
    mp3tag_collection_t *existing = NULL;
    mp3tag_read_tags(ctx, &existing);  /* OK if this fails (no tags yet) */

    /* Build a new working collection */
    mp3tag_collection_t *work = calloc(1, sizeof(*work));
    if (!work) return MP3TAG_ERR_NO_MEMORY;

    mp3tag_tag_t *wtag = calloc(1, sizeof(*wtag));
    if (!wtag) { free(work); return MP3TAG_ERR_NO_MEMORY; }
    wtag->target_type = MP3TAG_TARGET_ALBUM;
    work->tags  = wtag;
    work->count = 1;

    /* Copy existing simple tags, skipping the one we're replacing */
    if (existing) {
        for (const mp3tag_tag_t *tag = existing->tags; tag; tag = tag->next) {
            for (const mp3tag_simple_tag_t *st = tag->simple_tags; st; st = st->next) {
                if (st->name && str_casecmp(st->name, name) == 0)
                    continue;  /* skip — we'll add the new value below */

                mp3tag_simple_tag_t *copy = clone_simple_tag(st);
                if (copy) {
                    if (!wtag->simple_tags) {
                        wtag->simple_tags = copy;
                    } else {
                        mp3tag_simple_tag_t *tail = wtag->simple_tags;
                        while (tail->next) tail = tail->next;
                        tail->next = copy;
                    }
                }
            }
        }
    }

    /* Add the new tag (if value is non-NULL) */
    if (value) {
        mp3tag_simple_tag_t *st = calloc(1, sizeof(*st));
        if (!st) { free_collection(work); return MP3TAG_ERR_NO_MEMORY; }
        st->name  = str_dup(name);
        st->value = str_dup(value);

        if (!wtag->simple_tags) {
            wtag->simple_tags = st;
        } else {
            mp3tag_simple_tag_t *tail = wtag->simple_tags;
            while (tail->next) tail = tail->next;
            tail->next = st;
        }
    }

    int rc = mp3tag_write_tags(ctx, work);
    free_collection(work);
    return rc;
}

int mp3tag_remove_tag(mp3tag_context_t *ctx, const char *name)
{
    return mp3tag_set_tag_string(ctx, name, NULL);
}

/* ------------------------------------------------------------------ */
/*  Collection building API                                            */
/* ------------------------------------------------------------------ */

mp3tag_collection_t *mp3tag_collection_create(mp3tag_context_t *ctx)
{
    (void)ctx;
    mp3tag_collection_t *coll = calloc(1, sizeof(*coll));
    return coll;
}

void mp3tag_collection_free(mp3tag_context_t *ctx, mp3tag_collection_t *coll)
{
    (void)ctx;
    free_collection(coll);
}

mp3tag_tag_t *mp3tag_collection_add_tag(mp3tag_context_t *ctx,
                                        mp3tag_collection_t *coll,
                                        mp3tag_target_type_t type)
{
    (void)ctx;
    if (!coll) return NULL;

    mp3tag_tag_t *tag = calloc(1, sizeof(*tag));
    if (!tag) return NULL;

    tag->target_type = type;

    /* Append to list */
    if (!coll->tags) {
        coll->tags = tag;
    } else {
        mp3tag_tag_t *tail = coll->tags;
        while (tail->next) tail = tail->next;
        tail->next = tag;
    }
    coll->count++;

    return tag;
}

mp3tag_simple_tag_t *mp3tag_tag_add_simple(mp3tag_context_t *ctx,
                                           mp3tag_tag_t *tag,
                                           const char *name,
                                           const char *value)
{
    (void)ctx;
    if (!tag || !name) return NULL;

    mp3tag_simple_tag_t *st = calloc(1, sizeof(*st));
    if (!st) return NULL;

    st->name  = str_dup(name);
    st->value = value ? str_dup(value) : NULL;

    if (!st->name) {
        free(st);
        return NULL;
    }

    /* Append */
    if (!tag->simple_tags) {
        tag->simple_tags = st;
    } else {
        mp3tag_simple_tag_t *tail = tag->simple_tags;
        while (tail->next) tail = tail->next;
        tail->next = st;
    }

    return st;
}

mp3tag_simple_tag_t *mp3tag_simple_tag_add_nested(mp3tag_context_t *ctx,
                                                  mp3tag_simple_tag_t *parent,
                                                  const char *name,
                                                  const char *value)
{
    (void)ctx;
    if (!parent || !name) return NULL;

    mp3tag_simple_tag_t *st = calloc(1, sizeof(*st));
    if (!st) return NULL;

    st->name  = str_dup(name);
    st->value = value ? str_dup(value) : NULL;

    if (!st->name) {
        free(st);
        return NULL;
    }

    /* Append to parent's nested list */
    if (!parent->nested) {
        parent->nested = st;
    } else {
        mp3tag_simple_tag_t *tail = parent->nested;
        while (tail->next) tail = tail->next;
        tail->next = st;
    }

    return st;
}

int mp3tag_simple_tag_set_language(mp3tag_context_t *ctx,
                                  mp3tag_simple_tag_t *simple_tag,
                                  const char *language)
{
    (void)ctx;
    if (!simple_tag) return MP3TAG_ERR_INVALID_ARG;

    free(simple_tag->language);
    simple_tag->language = language ? str_dup(language) : NULL;
    return MP3TAG_OK;
}

int mp3tag_tag_add_track_uid(mp3tag_context_t *ctx, mp3tag_tag_t *tag,
                             uint64_t uid)
{
    (void)ctx;
    if (!tag) return MP3TAG_ERR_INVALID_ARG;

    uint64_t *new_uids = realloc(tag->track_uids,
                                  (tag->track_uid_count + 1) * sizeof(uint64_t));
    if (!new_uids) return MP3TAG_ERR_NO_MEMORY;

    new_uids[tag->track_uid_count] = uid;
    tag->track_uids = new_uids;
    tag->track_uid_count++;

    return MP3TAG_OK;
}
