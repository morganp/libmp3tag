/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2025 Morgan Prior */

/*
 * Comprehensive tests for libmp3tag across all supported formats:
 *   MP3  — raw stream with prepended ID3v2
 *   AAC  — raw ADTS stream with prepended ID3v2
 *   WAV  — RIFF/WAVE container with "id3 " chunk
 *   AIFF — IFF/AIFF container with "ID3 " chunk
 */

#include <mp3tag/mp3tag.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { printf("  PASS: %s\n", msg); g_pass++; } \
    else      { printf("  FAIL: %s\n", msg); g_fail++; } \
} while (0)

#define CHECK_RC(rc, msg) CHECK((rc) == MP3TAG_OK, msg)

/* ------------------------------------------------------------------ */
/*  Minimal test-file generators                                       */
/* ------------------------------------------------------------------ */

/* Helper: write raw bytes to a file */
static void write_bytes(FILE *f, const void *data, size_t n)
{
    fwrite(data, 1, n, f);
}

static void write_be16(FILE *f, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };
    write_bytes(f, b, 2);
}

static void write_be32(FILE *f, uint32_t v)
{
    uint8_t b[4] = {
        (uint8_t)(v >> 24), (uint8_t)(v >> 16),
        (uint8_t)(v >> 8),  (uint8_t)v
    };
    write_bytes(f, b, 4);
}

static void write_le16(FILE *f, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    write_bytes(f, b, 2);
}

static void write_le32(FILE *f, uint32_t v)
{
    uint8_t b[4] = {
        (uint8_t)v,          (uint8_t)(v >> 8),
        (uint8_t)(v >> 16),  (uint8_t)(v >> 24)
    };
    write_bytes(f, b, 4);
}

/*
 * Minimal MP3: a single silent MPEG1-Layer3 frame (417 bytes).
 */
static void create_mp3(const char *path)
{
    FILE *f = fopen(path, "wb");
    uint8_t frame[417];
    memset(frame, 0, sizeof(frame));
    frame[0] = 0xFF;  /* sync */
    frame[1] = 0xFB;  /* MPEG1, Layer3, no CRC */
    frame[2] = 0x90;  /* 128 kbps, 44100 Hz */
    frame[3] = 0x00;
    write_bytes(f, frame, sizeof(frame));
    fclose(f);
}

/*
 * Minimal AAC: a single ADTS frame header (7 bytes) + 1 byte data.
 */
static void create_aac(const char *path)
{
    FILE *f = fopen(path, "wb");
    /*
     * ADTS header (7 bytes, no CRC):
     *   syncword=FFF, ID=0(MPEG4), layer=00, protection=1
     *   profile=01(AAC-LC), sf_index=0100(44100), private=0, ch=001(mono)
     *   ... frame_length=8 (7 header + 1 data), buffer_fullness, frames_minus_1
     */
    uint8_t adts[8] = {
        0xFF, 0xF1,       /* sync + MPEG4 + no CRC */
        0x50,             /* AAC-LC, 44100, private=0, ch_top=0 */
        0x80,             /* ch_rest=01, orig=0, home=0, frame_len top bits */
        0x04, 0x00,       /* frame_length=8, buffer fullness */
        0x00,             /* frames_minus_1=0 */
        0x00              /* 1 byte of "audio" data */
    };
    /* Fix frame_length field: 8 = 0b0000000001000
       byte3 bits[1:0] = 00, byte4 = 0b00001000, byte5 bits[7:5] = 000 */
    adts[3] = 0x80;  /* ch=01, frame_length high 2 bits = 00 */
    adts[4] = 0x04;  /* frame_length mid 8 bits: 8>>3 = 1 -> 0x04 shifted */
    /* Actually let's just use a minimal frame length encoding: */
    /* frame_length = 8. In ADTS: bits spread across bytes 3-5 */
    /* byte3[1:0]=0, byte4[7:0]=0x02, byte5[7:5]=0 gives 8 */
    adts[3] = 0x80;
    adts[4] = 0x02;
    adts[5] = 0x00;
    write_bytes(f, adts, sizeof(adts));
    fclose(f);
}

/*
 * Minimal WAV: RIFF/WAVE with fmt + data chunks.
 * 1 channel, 16-bit, 44100 Hz, 1 sample of silence.
 */
static void create_wav(const char *path)
{
    FILE *f = fopen(path, "wb");

    /* RIFF header */
    write_bytes(f, "RIFF", 4);
    write_le32(f, 36 + 2);       /* total size: 4(WAVE) + 24(fmt) + 8(data hdr) + 2(data) */
    write_bytes(f, "WAVE", 4);

    /* fmt chunk: 16 bytes of PCM format */
    write_bytes(f, "fmt ", 4);
    write_le32(f, 16);           /* chunk size */
    write_le16(f, 1);            /* PCM */
    write_le16(f, 1);            /* mono */
    write_le32(f, 44100);        /* sample rate */
    write_le32(f, 88200);        /* byte rate */
    write_le16(f, 2);            /* block align */
    write_le16(f, 16);           /* bits per sample */

    /* data chunk: 1 sample = 2 bytes */
    write_bytes(f, "data", 4);
    write_le32(f, 2);
    write_le16(f, 0);            /* silence */

    fclose(f);
}

/*
 * Minimal AIFF: FORM/AIFF with COMM + SSND chunks.
 * 1 channel, 16-bit, 44100 Hz, 1 frame of silence.
 */
static void create_aiff(const char *path)
{
    FILE *f = fopen(path, "wb");

    /* FORM header */
    write_bytes(f, "FORM", 4);
    /* total = 4(AIFF) + 26(COMM chunk) + 18(SSND chunk) = 48 */
    write_be32(f, 48);
    write_bytes(f, "AIFF", 4);

    /* COMM chunk: 18 bytes of data */
    write_bytes(f, "COMM", 4);
    write_be32(f, 18);
    write_be16(f, 1);            /* numChannels */
    write_be32(f, 1);            /* numSampleFrames */
    write_be16(f, 16);           /* sampleSize */
    /* sampleRate as 80-bit IEEE 754 extended: 44100 Hz */
    uint8_t sr[10] = { 0x40, 0x0E, 0xAC, 0x44, 0, 0, 0, 0, 0, 0 };
    write_bytes(f, sr, 10);

    /* SSND chunk: offset(4) + blockSize(4) + 2 bytes audio = 10 bytes data */
    write_bytes(f, "SSND", 4);
    write_be32(f, 10);
    write_be32(f, 0);            /* offset */
    write_be32(f, 0);            /* blockSize */
    write_be16(f, 0);            /* silence */

    fclose(f);
}

/* ------------------------------------------------------------------ */
/*  Per-format test suite                                              */
/* ------------------------------------------------------------------ */

static void test_format(const char *label, const char *path,
                        void (*create_fn)(const char *))
{
    printf("\n--- %s ---\n", label);
    int rc;
    char buf[256];

    create_fn(path);

    mp3tag_context_t *ctx = mp3tag_create(NULL);
    CHECK(ctx != NULL, "create context");

    /* Open read-write */
    rc = mp3tag_open_rw(ctx, path);
    CHECK_RC(rc, "open_rw");

    /* No tags initially */
    rc = mp3tag_read_tag_string(ctx, "TITLE", buf, sizeof(buf));
    CHECK(rc == MP3TAG_ERR_NO_TAGS || rc == MP3TAG_ERR_TAG_NOT_FOUND,
          "no tags on fresh file");

    /* Write first tag (triggers rewrite / append for containers) */
    rc = mp3tag_set_tag_string(ctx, "TITLE", "Test Title");
    CHECK_RC(rc, "set TITLE");

    /* Read it back */
    rc = mp3tag_read_tag_string(ctx, "TITLE", buf, sizeof(buf));
    CHECK(rc == MP3TAG_OK && strcmp(buf, "Test Title") == 0,
          "read TITLE back");

    /* Write more tags */
    rc = mp3tag_set_tag_string(ctx, "ARTIST", "Test Artist");
    CHECK_RC(rc, "set ARTIST");
    rc = mp3tag_set_tag_string(ctx, "ALBUM", "Test Album");
    CHECK_RC(rc, "set ALBUM");
    rc = mp3tag_set_tag_string(ctx, "TRACK_NUMBER", "7");
    CHECK_RC(rc, "set TRACK_NUMBER");

    /* Verify all tags present */
    rc = mp3tag_read_tag_string(ctx, "ARTIST", buf, sizeof(buf));
    CHECK(rc == MP3TAG_OK && strcmp(buf, "Test Artist") == 0,
          "read ARTIST");
    rc = mp3tag_read_tag_string(ctx, "ALBUM", buf, sizeof(buf));
    CHECK(rc == MP3TAG_OK && strcmp(buf, "Test Album") == 0,
          "read ALBUM");
    rc = mp3tag_read_tag_string(ctx, "TRACK_NUMBER", buf, sizeof(buf));
    CHECK(rc == MP3TAG_OK && strcmp(buf, "7") == 0,
          "read TRACK_NUMBER");

    /* In-place update (should fit in padding) */
    rc = mp3tag_set_tag_string(ctx, "TITLE", "Updated");
    CHECK_RC(rc, "in-place update TITLE");
    rc = mp3tag_read_tag_string(ctx, "TITLE", buf, sizeof(buf));
    CHECK(rc == MP3TAG_OK && strcmp(buf, "Updated") == 0,
          "read updated TITLE");

    /* Remove a tag */
    rc = mp3tag_remove_tag(ctx, "TRACK_NUMBER");
    CHECK_RC(rc, "remove TRACK_NUMBER");
    rc = mp3tag_read_tag_string(ctx, "TRACK_NUMBER", buf, sizeof(buf));
    CHECK(rc == MP3TAG_ERR_TAG_NOT_FOUND, "TRACK_NUMBER removed");

    /* Close and reopen read-only to verify persistence */
    mp3tag_close(ctx);
    rc = mp3tag_open(ctx, path);
    CHECK_RC(rc, "reopen read-only");

    rc = mp3tag_read_tag_string(ctx, "TITLE", buf, sizeof(buf));
    CHECK(rc == MP3TAG_OK && strcmp(buf, "Updated") == 0,
          "persistent TITLE");
    rc = mp3tag_read_tag_string(ctx, "ARTIST", buf, sizeof(buf));
    CHECK(rc == MP3TAG_OK && strcmp(buf, "Test Artist") == 0,
          "persistent ARTIST");
    rc = mp3tag_read_tag_string(ctx, "ALBUM", buf, sizeof(buf));
    CHECK(rc == MP3TAG_OK && strcmp(buf, "Test Album") == 0,
          "persistent ALBUM");

    /* Close and reopen for collection API test */
    mp3tag_close(ctx);
    rc = mp3tag_open_rw(ctx, path);
    CHECK_RC(rc, "reopen read-write");

    mp3tag_collection_t *coll = mp3tag_collection_create(ctx);
    mp3tag_tag_t *tag = mp3tag_collection_add_tag(ctx, coll, MP3TAG_TARGET_ALBUM);
    mp3tag_tag_add_simple(ctx, tag, "TITLE",  "Collection Title");
    mp3tag_tag_add_simple(ctx, tag, "ARTIST", "Collection Artist");
    mp3tag_tag_add_simple(ctx, tag, "GENRE",  "Rock");

    rc = mp3tag_write_tags(ctx, coll);
    mp3tag_collection_free(ctx, coll);
    CHECK_RC(rc, "write_tags with collection");

    rc = mp3tag_read_tag_string(ctx, "TITLE", buf, sizeof(buf));
    CHECK(rc == MP3TAG_OK && strcmp(buf, "Collection Title") == 0,
          "collection TITLE");
    rc = mp3tag_read_tag_string(ctx, "GENRE", buf, sizeof(buf));
    CHECK(rc == MP3TAG_OK && strcmp(buf, "Rock") == 0,
          "collection GENRE");

    /* Read all tags and count */
    mp3tag_collection_t *all = NULL;
    rc = mp3tag_read_tags(ctx, &all);
    if (rc == MP3TAG_OK && all && all->tags) {
        int count = 0;
        for (mp3tag_simple_tag_t *st = all->tags->simple_tags; st; st = st->next)
            count++;
        CHECK(count == 3, "read_tags returned 3 simple tags");
    } else {
        CHECK(0, "read_tags returned collection");
    }

    mp3tag_close(ctx);
    mp3tag_destroy(ctx);
    remove(path);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(void)
{
    printf("libmp3tag v%s — multi-format test suite\n", mp3tag_version());
    printf("==========================================\n");

    test_format("MP3",  "/tmp/test_libmp3tag.mp3",  create_mp3);
    test_format("AAC",  "/tmp/test_libmp3tag.aac",  create_aac);
    test_format("WAV",  "/tmp/test_libmp3tag.wav",  create_wav);
    test_format("AIFF", "/tmp/test_libmp3tag.aiff", create_aiff);

    printf("\n==========================================\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
