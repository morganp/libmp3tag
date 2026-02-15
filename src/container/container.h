/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#ifndef CONTAINER_H
#define CONTAINER_H

#include <tag_common/file_io.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONTAINER_NONE = 0,   /* Raw stream: MP3, AAC, etc. (ID3v2 prepended) */
    CONTAINER_AIFF,       /* IFF/AIFF: ID3v2 in "ID3 " chunk */
    CONTAINER_WAV,        /* RIFF/WAVE: ID3v2 in "id3 " chunk */
    CONTAINER_AVI         /* RIFF/AVI:  ID3v2 in "id3 " chunk */
} container_type_t;

typedef struct {
    container_type_t type;

    /* FORM/RIFF size field (offset 4 in file, value excludes first 8 bytes) */
    uint32_t form_total_size;

    /* ID3 chunk location within the container */
    int      has_id3_chunk;
    int64_t  id3_chunk_offset;      /* Offset of chunk header (ID field) */
    uint32_t id3_chunk_data_size;   /* Data size from chunk header */
    int64_t  id3_chunk_data_offset; /* Offset of chunk data start */
} container_info_t;

/*
 * Detect container format and locate the ID3 chunk (if any).
 * For non-container files (MP3/AAC), sets type = CONTAINER_NONE.
 */
int container_detect(file_handle_t *fh, container_info_t *info);

/*
 * Append a new ID3 chunk at the end of a container file.
 * Updates the FORM/RIFF total size. Updates `info` in place.
 */
int container_append_id3(file_handle_t *fh, container_info_t *info,
                         const uint8_t *tag_data, uint32_t tag_size);

/*
 * Rewrite the container file, replacing the old ID3 chunk with new data.
 * Uses a temp file + rename. Reopens the file handle.
 * `fh_ptr` is updated to point to the new file handle.
 * `info` is updated with the new chunk location.
 */
int container_rewrite_id3(file_handle_t **fh_ptr, const char *path,
                          int writable, container_info_t *info,
                          const uint8_t *tag_data, uint32_t tag_size);

#ifdef __cplusplus
}
#endif

#endif /* CONTAINER_H */
