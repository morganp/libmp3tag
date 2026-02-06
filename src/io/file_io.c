/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#include "file_io.h"
#include "../../include/mp3tag/mp3tag_error.h"

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define READ_BUF_SIZE 8192

struct file_handle {
    int     fd;
    int     writable;
    int64_t file_size;
    int64_t pos;            /* Logical position */

    /* Read buffer */
    uint8_t buf[READ_BUF_SIZE];
    int64_t buf_start;      /* File offset of first byte in buf */
    size_t  buf_len;        /* Valid bytes in buf */
};

static file_handle_t *file_open_common(const char *path, int flags, int writable)
{
    int fd = open(path, flags, 0644);
    if (fd < 0)
        return NULL;

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return NULL;
    }

    file_handle_t *fh = calloc(1, sizeof(*fh));
    if (!fh) {
        close(fd);
        return NULL;
    }

    fh->fd        = fd;
    fh->writable  = writable;
    fh->file_size = st.st_size;
    fh->pos       = 0;
    fh->buf_start = 0;
    fh->buf_len   = 0;

    return fh;
}

file_handle_t *file_open_read(const char *path)
{
    return file_open_common(path, O_RDONLY, 0);
}

file_handle_t *file_open_rw(const char *path)
{
    return file_open_common(path, O_RDWR, 1);
}

void file_close(file_handle_t *fh)
{
    if (!fh) return;
    if (fh->fd >= 0)
        close(fh->fd);
    free(fh);
}

int64_t file_size(file_handle_t *fh)
{
    if (!fh) return -1;
    return fh->file_size;
}

int file_seek(file_handle_t *fh, int64_t offset)
{
    if (!fh) return MP3TAG_ERR_INVALID_ARG;
    fh->pos = offset;
    return 0;
}

int64_t file_tell(file_handle_t *fh)
{
    if (!fh) return -1;
    return fh->pos;
}

int file_read(file_handle_t *fh, void *buf, size_t size)
{
    if (!fh || (!buf && size > 0))
        return MP3TAG_ERR_INVALID_ARG;

    uint8_t *dst = (uint8_t *)buf;
    size_t remaining = size;

    while (remaining > 0) {
        /* Check if current pos is within the read buffer */
        if (fh->buf_len > 0 &&
            fh->pos >= fh->buf_start &&
            fh->pos < fh->buf_start + (int64_t)fh->buf_len)
        {
            size_t offset = (size_t)(fh->pos - fh->buf_start);
            size_t avail  = fh->buf_len - offset;
            size_t to_copy = remaining < avail ? remaining : avail;
            memcpy(dst, fh->buf + offset, to_copy);
            dst       += to_copy;
            fh->pos   += (int64_t)to_copy;
            remaining -= to_copy;
            continue;
        }

        /* Refill buffer from the current position */
        off_t seeked = lseek(fh->fd, (off_t)fh->pos, SEEK_SET);
        if (seeked < 0)
            return MP3TAG_ERR_SEEK_FAILED;

        ssize_t n = read(fh->fd, fh->buf, READ_BUF_SIZE);
        if (n < 0)
            return MP3TAG_ERR_IO;
        if (n == 0)
            return MP3TAG_ERR_TRUNCATED;

        fh->buf_start = fh->pos;
        fh->buf_len   = (size_t)n;
    }

    return 0;
}

int64_t file_read_partial(file_handle_t *fh, void *buf, size_t size)
{
    if (!fh || !buf) return -1;

    /* For partial reads, bypass the buffer and read directly */
    off_t seeked = lseek(fh->fd, (off_t)fh->pos, SEEK_SET);
    if (seeked < 0) return -1;

    ssize_t n = read(fh->fd, buf, size);
    if (n < 0) return -1;

    fh->pos += n;

    /* Invalidate read buffer since fd position changed */
    fh->buf_len = 0;

    return (int64_t)n;
}

int file_write(file_handle_t *fh, const void *buf, size_t size)
{
    if (!fh || (!buf && size > 0))
        return MP3TAG_ERR_INVALID_ARG;
    if (!fh->writable)
        return MP3TAG_ERR_READ_ONLY;

    /* Seek the fd to the logical position */
    off_t seeked = lseek(fh->fd, (off_t)fh->pos, SEEK_SET);
    if (seeked < 0)
        return MP3TAG_ERR_SEEK_FAILED;

    const uint8_t *src = (const uint8_t *)buf;
    size_t remaining = size;

    while (remaining > 0) {
        ssize_t n = write(fh->fd, src, remaining);
        if (n < 0)
            return MP3TAG_ERR_WRITE_FAILED;
        src       += n;
        remaining -= (size_t)n;
        fh->pos   += n;
    }

    /* Invalidate read buffer since file content may have changed */
    fh->buf_len = 0;

    /* Update file size if we wrote past the end */
    if (fh->pos > fh->file_size)
        fh->file_size = fh->pos;

    return 0;
}

int file_sync(file_handle_t *fh)
{
    if (!fh) return MP3TAG_ERR_INVALID_ARG;
    return fsync(fh->fd) == 0 ? 0 : MP3TAG_ERR_IO;
}

int file_is_writable(file_handle_t *fh)
{
    return fh ? fh->writable : 0;
}
