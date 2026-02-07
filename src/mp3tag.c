/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#include "../include/mp3tag/mp3tag.h"
#include "id3v2/id3v2_reader.h"
#include "id3v2/id3v2_writer.h"
#include "id3v2/id3v2_defs.h"
#include "id3v1/id3v1.h"
#include "container/container.h"
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

    /* Container format info */
    container_info_t    container;

    /* Parsed file structure */
    int                 has_id3v2;
    id3v2_header_t      id3v2_hdr;
    int64_t             id3v2_offset;  /* File offset of ID3v2 header */
    int64_t             audio_offset;  /* First byte of audio (raw streams only) */

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
    return "1.1.0";
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
    case MP3TAG_ERR_NOT_MP3:       return "Not a supported file or no ID3 tag";
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
    /* Detect container format (AIFF, WAV, or raw stream) */
    int rc = container_detect(ctx->fh, &ctx->container);
    if (rc != MP3TAG_OK)
        return rc;

    if (ctx->container.type == CONTAINER_NONE) {
        /* Raw stream (MP3, AAC, etc.) — ID3v2 is prepended at offset 0 */
        rc = id3v2_read_header(ctx->fh, 0, &ctx->id3v2_hdr);
        if (rc == MP3TAG_OK) {
            ctx->has_id3v2    = 1;
            ctx->id3v2_offset = 0;
            ctx->audio_offset = ID3V2_HEADER_SIZE + ctx->id3v2_hdr.tag_size;
            if (ctx->id3v2_hdr.has_footer)
                ctx->audio_offset += ID3V2_FOOTER_SIZE;
        } else {
            ctx->has_id3v2    = 0;
            ctx->id3v2_offset = 0;
            ctx->audio_offset = 0;
        }

        /* Check for ID3v1 at end of file */
        int v1 = id3v1_detect(ctx->fh);
        ctx->has_id3v1 = (v1 == 1);
    } else {
        /* Container (AIFF/WAV) — ID3v2 is inside a chunk */
        ctx->has_id3v1 = 0;

        if (ctx->container.has_id3_chunk) {
            rc = id3v2_read_header(ctx->fh,
                                   ctx->container.id3_chunk_data_offset,
                                   &ctx->id3v2_hdr);
            if (rc == MP3TAG_OK) {
                ctx->has_id3v2    = 1;
                ctx->id3v2_offset = ctx->container.id3_chunk_data_offset;
            } else {
                ctx->has_id3v2 = 0;
            }
        } else {
            ctx->has_id3v2 = 0;
        }
    }

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
    memset(&ctx->container, 0, sizeof(ctx->container));
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
        int rc = id3v2_read_frames(ctx->fh, ctx->id3v2_offset,
                                   &ctx->id3v2_hdr, &frames);
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

    /* Fall back to ID3v1 (raw streams only) */
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
/*  Write helpers: zero-pad                                            */
/* ------------------------------------------------------------------ */

static int write_zeros(file_handle_t *fh, uint32_t count)
{
    uint8_t zeros[4096];
    memset(zeros, 0, sizeof(zeros));
    while (count > 0) {
        uint32_t chunk = count < sizeof(zeros) ? count : (uint32_t)sizeof(zeros);
        if (file_write(fh, zeros, chunk) != 0)
            return MP3TAG_ERR_WRITE_FAILED;
        count -= chunk;
    }
    return MP3TAG_OK;
}

/* ------------------------------------------------------------------ */
/*  Tag writing: raw stream (MP3/AAC)                                  */
/* ------------------------------------------------------------------ */

static int raw_try_inplace(mp3tag_context_t *ctx, dyn_buffer_t *frame_buf)
{
    if (!ctx->has_id3v2)
        return MP3TAG_ERR_NO_SPACE;

    uint32_t available = ctx->id3v2_hdr.tag_size;
    uint32_t needed    = (uint32_t)frame_buf->size;

    if (needed > available)
        return MP3TAG_ERR_NO_SPACE;

    uint8_t hdr[ID3V2_HEADER_SIZE];
    id3v2_build_header(available, hdr);

    if (file_seek(ctx->fh, 0) != 0)
        return MP3TAG_ERR_SEEK_FAILED;
    if (file_write(ctx->fh, hdr, ID3V2_HEADER_SIZE) != 0)
        return MP3TAG_ERR_WRITE_FAILED;
    if (file_write(ctx->fh, frame_buf->data, frame_buf->size) != 0)
        return MP3TAG_ERR_WRITE_FAILED;

    int rc = write_zeros(ctx->fh, available - needed);
    if (rc != MP3TAG_OK) return rc;

    file_sync(ctx->fh);
    return MP3TAG_OK;
}

static int raw_rewrite(mp3tag_context_t *ctx, dyn_buffer_t *frame_buf)
{
    if (!ctx->path)
        return MP3TAG_ERR_INVALID_ARG;

    size_t path_len = strlen(ctx->path);
    char *tmp_path = malloc(path_len + 5);
    if (!tmp_path) return MP3TAG_ERR_NO_MEMORY;
    memcpy(tmp_path, ctx->path, path_len);
    memcpy(tmp_path + path_len, ".tmp", 5);

    uint32_t body_size = (uint32_t)frame_buf->size + ID3V2_DEFAULT_PADDING;
    uint8_t hdr[ID3V2_HEADER_SIZE];
    id3v2_build_header(body_size, hdr);

    /* Create temp file */
    file_handle_t *tmp = file_open_rw(tmp_path);
    if (!tmp) {
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

    result = write_zeros(tmp, ID3V2_DEFAULT_PADDING);
    if (result != MP3TAG_OK) goto cleanup;

    /* Copy audio data from original */
    {
        int64_t src_offset = ctx->audio_offset;
        int64_t src_end    = file_size(ctx->fh);

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
            if (n <= 0) break;
            if (file_write(tmp, copy_buf, (size_t)n) != 0) {
                result = MP3TAG_ERR_WRITE_FAILED;
                goto cleanup;
            }
            bytes_left -= n;
        }
    }

    if (file_sync(tmp) != 0) { result = MP3TAG_ERR_IO; goto cleanup; }

    file_close(tmp); tmp = NULL;
    file_close(ctx->fh); ctx->fh = NULL;

    if (rename(tmp_path, ctx->path) != 0) {
        result = MP3TAG_ERR_RENAME_FAILED;
        ctx->fh = ctx->writable ? file_open_rw(ctx->path)
                                : file_open_read(ctx->path);
        goto cleanup_path;
    }

    ctx->fh = ctx->writable ? file_open_rw(ctx->path)
                            : file_open_read(ctx->path);
    if (!ctx->fh) { result = MP3TAG_ERR_IO; goto cleanup_path; }

    probe_file(ctx);

cleanup:
    if (tmp) { file_close(tmp); unlink(tmp_path); }
cleanup_path:
    free(tmp_path);
    return result;
}

/* ------------------------------------------------------------------ */
/*  Tag writing: container (AIFF/WAV)                                  */
/* ------------------------------------------------------------------ */

static int container_try_inplace(mp3tag_context_t *ctx,
                                 dyn_buffer_t *frame_buf)
{
    if (!ctx->container.has_id3_chunk)
        return MP3TAG_ERR_NO_SPACE;

    uint32_t available = ctx->container.id3_chunk_data_size;
    uint32_t needed    = ID3V2_HEADER_SIZE + (uint32_t)frame_buf->size;

    if (needed > available)
        return MP3TAG_ERR_NO_SPACE;

    /* Build ID3v2 header using the full chunk data size as body */
    uint8_t hdr[ID3V2_HEADER_SIZE];
    id3v2_build_header(available - ID3V2_HEADER_SIZE, hdr);

    int64_t data_off = ctx->container.id3_chunk_data_offset;
    if (file_seek(ctx->fh, data_off) != 0)
        return MP3TAG_ERR_SEEK_FAILED;
    if (file_write(ctx->fh, hdr, ID3V2_HEADER_SIZE) != 0)
        return MP3TAG_ERR_WRITE_FAILED;
    if (file_write(ctx->fh, frame_buf->data, frame_buf->size) != 0)
        return MP3TAG_ERR_WRITE_FAILED;

    int rc = write_zeros(ctx->fh, available - needed);
    if (rc != MP3TAG_OK) return rc;

    file_sync(ctx->fh);
    return MP3TAG_OK;
}

static int container_write_new(mp3tag_context_t *ctx, dyn_buffer_t *frame_buf)
{
    /* Build full ID3v2 tag (header + frames + padding) */
    uint32_t body_size = (uint32_t)frame_buf->size + ID3V2_DEFAULT_PADDING;
    uint32_t tag_total = ID3V2_HEADER_SIZE + body_size;

    uint8_t *tag_data = calloc(1, tag_total);
    if (!tag_data) return MP3TAG_ERR_NO_MEMORY;

    id3v2_build_header(body_size, tag_data);
    memcpy(tag_data + ID3V2_HEADER_SIZE, frame_buf->data, frame_buf->size);

    int rc;
    if (!ctx->container.has_id3_chunk) {
        /* No existing chunk — append */
        rc = container_append_id3(ctx->fh, &ctx->container,
                                  tag_data, tag_total);
    } else {
        /* Existing chunk too small — rewrite container */
        rc = container_rewrite_id3(&ctx->fh, ctx->path, ctx->writable,
                                   &ctx->container, tag_data, tag_total);
    }

    free(tag_data);

    if (rc == MP3TAG_OK)
        probe_file(ctx);

    return rc;
}

/* ------------------------------------------------------------------ */
/*  Tag writing: main entry point                                      */
/* ------------------------------------------------------------------ */

int mp3tag_write_tags(mp3tag_context_t *ctx, const mp3tag_collection_t *tags)
{
    if (!ctx || !tags)   return MP3TAG_ERR_INVALID_ARG;
    if (!ctx->fh)        return MP3TAG_ERR_NOT_OPEN;
    if (!ctx->writable)  return MP3TAG_ERR_READ_ONLY;

    dyn_buffer_t frame_buf;
    buffer_init(&frame_buf);

    int rc = id3v2_serialize_frames(tags, &frame_buf);
    if (rc != MP3TAG_OK) {
        buffer_free(&frame_buf);
        return rc;
    }

    invalidate_cache(ctx);

    if (ctx->container.type == CONTAINER_NONE) {
        /* Raw stream: try in-place, then rewrite */
        rc = raw_try_inplace(ctx, &frame_buf);
        if (rc == MP3TAG_OK) {
            buffer_free(&frame_buf);
            probe_file(ctx);
            return MP3TAG_OK;
        }
        rc = raw_rewrite(ctx, &frame_buf);
        buffer_free(&frame_buf);
        return rc;
    } else {
        /* Container: try in-place within chunk, then append/rewrite */
        rc = container_try_inplace(ctx, &frame_buf);
        if (rc == MP3TAG_OK) {
            buffer_free(&frame_buf);
            probe_file(ctx);
            return MP3TAG_OK;
        }
        rc = container_write_new(ctx, &frame_buf);
        buffer_free(&frame_buf);
        return rc;
    }
}

/* ------------------------------------------------------------------ */
/*  Convenience: set / remove single tag                               */
/* ------------------------------------------------------------------ */

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

    mp3tag_collection_t *existing = NULL;
    mp3tag_read_tags(ctx, &existing);

    mp3tag_collection_t *work = calloc(1, sizeof(*work));
    if (!work) return MP3TAG_ERR_NO_MEMORY;

    mp3tag_tag_t *wtag = calloc(1, sizeof(*wtag));
    if (!wtag) { free(work); return MP3TAG_ERR_NO_MEMORY; }
    wtag->target_type = MP3TAG_TARGET_ALBUM;
    work->tags  = wtag;
    work->count = 1;

    if (existing) {
        for (const mp3tag_tag_t *tag = existing->tags; tag; tag = tag->next) {
            for (const mp3tag_simple_tag_t *st = tag->simple_tags; st; st = st->next) {
                if (st->name && str_casecmp(st->name, name) == 0)
                    continue;

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
    return calloc(1, sizeof(mp3tag_collection_t));
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
    if (!st->name) { free(st); return NULL; }

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
    if (!st->name) { free(st); return NULL; }

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
