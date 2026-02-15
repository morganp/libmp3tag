/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#include "container.h"
#include "../../include/mp3tag/mp3tag_error.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/*  Endian helpers                                                     */
/* ------------------------------------------------------------------ */

static uint32_t read_be32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  | (uint32_t)b[3];
}

static uint32_t read_le32(const uint8_t *b)
{
    return (uint32_t)b[0]         | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void write_be32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)(v >> 24);
    b[1] = (uint8_t)(v >> 16);
    b[2] = (uint8_t)(v >> 8);
    b[3] = (uint8_t)v;
}

static void write_le32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

/* ------------------------------------------------------------------ */
/*  Chunk scanning                                                     */
/* ------------------------------------------------------------------ */

/*
 * Scan IFF/RIFF chunks looking for the ID3 chunk.
 * AIFF uses big-endian sizes and chunk ID "ID3 ".
 * WAV uses little-endian sizes and chunk ID "id3 ".
 */
static void scan_chunks(file_handle_t *fh, container_info_t *info)
{
    int is_aiff = (info->type == CONTAINER_AIFF);
    const char *target_id = is_aiff ? "ID3 " : "id3 ";

    int64_t pos = 12;  /* After FORM/RIFF(4) + size(4) + type(4) */
    int64_t end = 8 + (int64_t)info->form_total_size;
    int64_t fsize = file_size(fh);
    if (end > fsize) end = fsize;

    while (pos + 8 <= end) {
        uint8_t chdr[8];
        if (file_seek(fh, pos) != 0) break;
        if (file_read(fh, chdr, 8) != 0) break;

        uint32_t chunk_size = is_aiff ? read_be32(chdr + 4)
                                      : read_le32(chdr + 4);

        if (memcmp(chdr, target_id, 4) == 0) {
            info->has_id3_chunk       = 1;
            info->id3_chunk_offset    = pos;
            info->id3_chunk_data_size = chunk_size;
            info->id3_chunk_data_offset = pos + 8;
            return;
        }

        pos += 8 + chunk_size;
        if (chunk_size & 1) pos++;  /* IFF/RIFF pad byte */
    }
}

/* ------------------------------------------------------------------ */
/*  Detection                                                          */
/* ------------------------------------------------------------------ */

int container_detect(file_handle_t *fh, container_info_t *info)
{
    if (!fh || !info) return MP3TAG_ERR_INVALID_ARG;

    memset(info, 0, sizeof(*info));
    info->id3_chunk_offset = -1;

    uint8_t magic[12];
    if (file_seek(fh, 0) != 0) return MP3TAG_ERR_SEEK_FAILED;

    /* Need at least 12 bytes for container detection */
    if (file_size(fh) < 12) {
        info->type = CONTAINER_NONE;
        return MP3TAG_OK;
    }

    if (file_read(fh, magic, 12) != 0) {
        info->type = CONTAINER_NONE;
        return MP3TAG_OK;
    }

    /* AIFF / AIFC */
    if (memcmp(magic, "FORM", 4) == 0 &&
        (memcmp(magic + 8, "AIFF", 4) == 0 ||
         memcmp(magic + 8, "AIFC", 4) == 0))
    {
        info->type = CONTAINER_AIFF;
        info->form_total_size = read_be32(magic + 4);
        scan_chunks(fh, info);
        return MP3TAG_OK;
    }

    /* WAV */
    if (memcmp(magic, "RIFF", 4) == 0 &&
        memcmp(magic + 8, "WAVE", 4) == 0)
    {
        info->type = CONTAINER_WAV;
        info->form_total_size = read_le32(magic + 4);
        scan_chunks(fh, info);
        return MP3TAG_OK;
    }

    /* AVI */
    if (memcmp(magic, "RIFF", 4) == 0 &&
        memcmp(magic + 8, "AVI ", 4) == 0)
    {
        info->type = CONTAINER_AVI;
        info->form_total_size = read_le32(magic + 4);
        scan_chunks(fh, info);
        return MP3TAG_OK;
    }

    info->type = CONTAINER_NONE;
    return MP3TAG_OK;
}

/* ------------------------------------------------------------------ */
/*  Append ID3 chunk                                                   */
/* ------------------------------------------------------------------ */

int container_append_id3(file_handle_t *fh, container_info_t *info,
                         const uint8_t *tag_data, uint32_t tag_size)
{
    if (!fh || !info || !tag_data)
        return MP3TAG_ERR_INVALID_ARG;

    int is_aiff = (info->type == CONTAINER_AIFF);
    int64_t fsize = file_size(fh);

    /* Build chunk header */
    uint8_t chunk_hdr[8];
    memcpy(chunk_hdr, is_aiff ? "ID3 " : "id3 ", 4);
    if (is_aiff)
        write_be32(chunk_hdr + 4, tag_size);
    else
        write_le32(chunk_hdr + 4, tag_size);

    /* Seek to end and write */
    if (file_seek(fh, fsize) != 0)
        return MP3TAG_ERR_SEEK_FAILED;
    if (file_write(fh, chunk_hdr, 8) != 0)
        return MP3TAG_ERR_WRITE_FAILED;
    if (file_write(fh, tag_data, tag_size) != 0)
        return MP3TAG_ERR_WRITE_FAILED;

    /* Pad byte if chunk data is odd */
    if (tag_size & 1) {
        uint8_t pad = 0;
        if (file_write(fh, &pad, 1) != 0)
            return MP3TAG_ERR_WRITE_FAILED;
    }

    /* Update FORM/RIFF total size */
    uint32_t added = 8 + tag_size + (tag_size & 1 ? 1 : 0);
    uint32_t new_total = info->form_total_size + added;
    uint8_t size_bytes[4];

    if (is_aiff)
        write_be32(size_bytes, new_total);
    else
        write_le32(size_bytes, new_total);

    if (file_seek(fh, 4) != 0)
        return MP3TAG_ERR_SEEK_FAILED;
    if (file_write(fh, size_bytes, 4) != 0)
        return MP3TAG_ERR_WRITE_FAILED;

    if (file_sync(fh) != 0)
        return MP3TAG_ERR_IO;

    /* Update info */
    info->has_id3_chunk       = 1;
    info->id3_chunk_offset    = fsize;
    info->id3_chunk_data_size = tag_size;
    info->id3_chunk_data_offset = fsize + 8;
    info->form_total_size     = new_total;

    return MP3TAG_OK;
}

/* ------------------------------------------------------------------ */
/*  Rewrite container with new ID3 chunk                               */
/* ------------------------------------------------------------------ */

int container_rewrite_id3(file_handle_t **fh_ptr, const char *path,
                          int writable, container_info_t *info,
                          const uint8_t *tag_data, uint32_t tag_size)
{
    if (!fh_ptr || !*fh_ptr || !path || !info || !tag_data)
        return MP3TAG_ERR_INVALID_ARG;

    file_handle_t *fh = *fh_ptr;
    int is_aiff = (info->type == CONTAINER_AIFF);

    /* Build temp path */
    size_t path_len = strlen(path);
    char *tmp_path = malloc(path_len + 5);
    if (!tmp_path) return MP3TAG_ERR_NO_MEMORY;
    memcpy(tmp_path, path, path_len);
    memcpy(tmp_path + path_len, ".tmp", 5);

    /* Create temp file */
    FILE *f = fopen(tmp_path, "wb");
    if (!f) { free(tmp_path); return MP3TAG_ERR_IO; }
    fclose(f);

    file_handle_t *tmp = file_open_rw(tmp_path);
    if (!tmp) { free(tmp_path); return MP3TAG_ERR_IO; }

    int result = MP3TAG_OK;
    int64_t fsize = file_size(fh);

    /* Copy file header (12 bytes) with placeholder total size */
    uint8_t header[12];
    if (file_seek(fh, 0) != 0 || file_read(fh, header, 12) != 0) {
        result = MP3TAG_ERR_IO;
        goto cleanup;
    }
    if (file_write(tmp, header, 12) != 0) {
        result = MP3TAG_ERR_WRITE_FAILED;
        goto cleanup;
    }

    /* Iterate chunks, copying all except the old ID3 chunk */
    {
        const char *skip_id = is_aiff ? "ID3 " : "id3 ";
        int64_t pos = 12;
        int64_t end = 8 + (int64_t)info->form_total_size;
        if (end > fsize) end = fsize;

        while (pos + 8 <= end) {
            uint8_t chdr[8];
            if (file_seek(fh, pos) != 0) break;
            if (file_read(fh, chdr, 8) != 0) break;

            uint32_t chunk_size = is_aiff ? read_be32(chdr + 4)
                                          : read_le32(chdr + 4);
            uint32_t chunk_total = 8 + chunk_size + (chunk_size & 1 ? 1 : 0);

            if (memcmp(chdr, skip_id, 4) == 0) {
                pos += chunk_total;
                continue;
            }

            /* Copy this chunk */
            if (file_seek(fh, pos) != 0) {
                result = MP3TAG_ERR_IO;
                goto cleanup;
            }

            uint8_t copy_buf[65536];
            uint32_t remaining = chunk_total;
            while (remaining > 0) {
                size_t to_read = remaining < sizeof(copy_buf)
                                 ? remaining : sizeof(copy_buf);
                int64_t n = file_read_partial(fh, copy_buf, to_read);
                if (n <= 0) break;
                if (file_write(tmp, copy_buf, (size_t)n) != 0) {
                    result = MP3TAG_ERR_WRITE_FAILED;
                    goto cleanup;
                }
                remaining -= (uint32_t)n;
            }

            pos += chunk_total;
        }
    }

    /* Append new ID3 chunk */
    {
        uint8_t new_chunk_hdr[8];
        memcpy(new_chunk_hdr, is_aiff ? "ID3 " : "id3 ", 4);
        if (is_aiff)
            write_be32(new_chunk_hdr + 4, tag_size);
        else
            write_le32(new_chunk_hdr + 4, tag_size);

        int64_t new_chunk_off = file_tell(tmp);

        if (file_write(tmp, new_chunk_hdr, 8) != 0 ||
            file_write(tmp, tag_data, tag_size) != 0) {
            result = MP3TAG_ERR_WRITE_FAILED;
            goto cleanup;
        }

        if (tag_size & 1) {
            uint8_t pad = 0;
            if (file_write(tmp, &pad, 1) != 0) {
                result = MP3TAG_ERR_WRITE_FAILED;
                goto cleanup;
            }
        }

        /* Update FORM/RIFF total size */
        int64_t new_fsize = file_tell(tmp);
        uint32_t new_total = (uint32_t)(new_fsize - 8);
        uint8_t size_bytes[4];

        if (is_aiff)
            write_be32(size_bytes, new_total);
        else
            write_le32(size_bytes, new_total);

        if (file_seek(tmp, 4) != 0 ||
            file_write(tmp, size_bytes, 4) != 0) {
            result = MP3TAG_ERR_WRITE_FAILED;
            goto cleanup;
        }

        if (file_sync(tmp) != 0) {
            result = MP3TAG_ERR_IO;
            goto cleanup;
        }

        /* Close both files before rename */
        file_close(tmp);
        tmp = NULL;
        file_close(fh);
        *fh_ptr = NULL;

        if (rename(tmp_path, path) != 0) {
            result = MP3TAG_ERR_RENAME_FAILED;
            *fh_ptr = writable ? file_open_rw(path) : file_open_read(path);
            goto cleanup_path;
        }

        *fh_ptr = writable ? file_open_rw(path) : file_open_read(path);
        if (!*fh_ptr) {
            result = MP3TAG_ERR_IO;
            goto cleanup_path;
        }

        /* Update info */
        info->form_total_size     = new_total;
        info->has_id3_chunk       = 1;
        info->id3_chunk_offset    = new_chunk_off;
        info->id3_chunk_data_size = tag_size;
        info->id3_chunk_data_offset = new_chunk_off + 8;
    }

cleanup:
    if (tmp) {
        file_close(tmp);
        unlink(tmp_path);
    }
cleanup_path:
    free(tmp_path);
    return result;
}
