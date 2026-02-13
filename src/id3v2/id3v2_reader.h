/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#ifndef ID3V2_READER_H
#define ID3V2_READER_H

#include <tag_common/file_io.h>
#include "../../include/mp3tag/mp3tag_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parsed ID3v2 header information */
typedef struct {
    uint8_t  version_major;    /* 3 or 4 */
    uint8_t  version_revision;
    uint8_t  flags;
    uint32_t tag_size;         /* Excluding 10-byte header (and footer) */
    int      has_footer;
} id3v2_header_t;

/* A parsed ID3v2 frame */
typedef struct id3v2_frame {
    char     id[5];            /* 4-char frame ID + NUL */
    uint8_t *data;             /* Raw frame content (after header) */
    uint32_t data_size;
    uint16_t flags;

    struct id3v2_frame *next;
} id3v2_frame_t;

/*
 * Read and validate the ID3v2 header at the given file offset.
 * Returns MP3TAG_OK on success, MP3TAG_ERR_NOT_MP3 if no ID3v2 header.
 */
int id3v2_read_header(file_handle_t *fh, int64_t offset, id3v2_header_t *hdr);

/*
 * Read all frames from an ID3v2 tag.
 * `base_offset` is the file offset where the ID3v2 header starts.
 * Returns a linked list of frames; caller must free with id3v2_free_frames().
 */
int id3v2_read_frames(file_handle_t *fh, int64_t base_offset,
                      const id3v2_header_t *hdr, id3v2_frame_t **frames);

/*
 * Convert parsed ID3v2 frames into an mp3tag_collection_t.
 */
int id3v2_frames_to_collection(const id3v2_frame_t *frames,
                               mp3tag_collection_t **coll);

/*
 * Free a linked list of frames.
 */
void id3v2_free_frames(id3v2_frame_t *frames);

#ifdef __cplusplus
}
#endif

#endif /* ID3V2_READER_H */
