/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#ifndef ID3V2_WRITER_H
#define ID3V2_WRITER_H

#include <tag_common/buffer.h>
#include "../../include/mp3tag/mp3tag_types.h"
#include "id3v2_reader.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Serialize an mp3tag_collection_t into an ID3v2.4 tag body (frames only,
 * no header or padding). The result is written into `buf`.
 */
int id3v2_serialize_frames(const mp3tag_collection_t *coll, dyn_buffer_t *buf);

/*
 * Build a complete ID3v2.4 header for the given body size (frames + padding).
 * Writes exactly 10 bytes into `hdr_out`.
 */
void id3v2_build_header(uint32_t body_size, uint8_t hdr_out[10]);

#ifdef __cplusplus
}
#endif

#endif /* ID3V2_WRITER_H */
