/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

#ifndef MP3TAG_ERROR_H
#define MP3TAG_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Success */
#define MP3TAG_OK                  0

/* General errors */
#define MP3TAG_ERR_INVALID_ARG    -1
#define MP3TAG_ERR_NO_MEMORY      -2
#define MP3TAG_ERR_IO             -3
#define MP3TAG_ERR_NOT_OPEN       -4
#define MP3TAG_ERR_ALREADY_OPEN   -5
#define MP3TAG_ERR_READ_ONLY      -6

/* Format errors */
#define MP3TAG_ERR_NOT_MP3        -10
#define MP3TAG_ERR_BAD_ID3V2      -11
#define MP3TAG_ERR_CORRUPT        -12
#define MP3TAG_ERR_TRUNCATED      -13
#define MP3TAG_ERR_UNSUPPORTED    -14

/* Tag errors */
#define MP3TAG_ERR_NO_TAGS        -20
#define MP3TAG_ERR_TAG_NOT_FOUND  -21
#define MP3TAG_ERR_TAG_TOO_LARGE  -22

/* Write errors */
#define MP3TAG_ERR_NO_SPACE       -30
#define MP3TAG_ERR_WRITE_FAILED   -31
#define MP3TAG_ERR_SEEK_FAILED    -32
#define MP3TAG_ERR_RENAME_FAILED  -33

#ifdef __cplusplus
}
#endif

#endif /* MP3TAG_ERROR_H */
