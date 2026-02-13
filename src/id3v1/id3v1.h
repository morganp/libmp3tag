/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#ifndef ID3V1_H
#define ID3V1_H

#include <tag_common/file_io.h>
#include "../../include/mp3tag/mp3tag_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Size of an ID3v1 tag at the end of the file */
#define ID3V1_TAG_SIZE 128

/*
 * Check if the file has an ID3v1 tag (last 128 bytes start with "TAG").
 * Returns 1 if present, 0 if not, or a negative error code.
 */
int id3v1_detect(file_handle_t *fh);

/*
 * Read the ID3v1 tag and convert to an mp3tag_collection_t.
 * Returns MP3TAG_ERR_NO_TAGS if no ID3v1 tag is present.
 */
int id3v1_read(file_handle_t *fh, mp3tag_collection_t **coll);

#ifdef __cplusplus
}
#endif

#endif /* ID3V1_H */
