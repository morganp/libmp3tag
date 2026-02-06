/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#ifndef FILE_IO_H
#define FILE_IO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque file handle */
typedef struct file_handle file_handle_t;

/* Open a file for reading only */
file_handle_t *file_open_read(const char *path);

/* Open a file for reading and writing */
file_handle_t *file_open_rw(const char *path);

/* Close and free a file handle */
void file_close(file_handle_t *fh);

/* Get the total file size in bytes */
int64_t file_size(file_handle_t *fh);

/* Seek to an absolute offset */
int file_seek(file_handle_t *fh, int64_t offset);

/* Get the current logical position */
int64_t file_tell(file_handle_t *fh);

/* Read exactly `size` bytes into `buf`. Returns 0 on success. */
int file_read(file_handle_t *fh, void *buf, size_t size);

/* Read up to `size` bytes. Returns the number of bytes read, or -1 on error. */
int64_t file_read_partial(file_handle_t *fh, void *buf, size_t size);

/* Write exactly `size` bytes from `buf`. Returns 0 on success. */
int file_write(file_handle_t *fh, const void *buf, size_t size);

/* Flush and sync to disk */
int file_sync(file_handle_t *fh);

/* Check if the file is writable */
int file_is_writable(file_handle_t *fh);

#ifdef __cplusplus
}
#endif

#endif /* FILE_IO_H */
