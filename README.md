# libmp3tag

A pure C library for reading and writing audio file metadata (ID3v2/ID3v1) tags without loading entire files into memory. Supports **MP3**, **AAC**, **WAV**, and **AIFF** files.

API-compatible with [libmkvtag](https://github.com/morganp/libmkvtag) — swap `mkvtag_` for `mp3tag_` and the same patterns apply.

## Features

- **Multi-format**: MP3, AAC (ADTS), WAV (RIFF), and AIFF containers — same API for all
- **Memory-efficient**: buffered I/O with 8KB read buffer; never loads the full audio into memory
- **In-place editing**: when the new tags fit within the existing ID3v2 space (including padding), the file is updated in place without rewriting
- **Safe rewrite**: when more space is needed, writes to a temp file then performs an atomic rename; adds 4KB padding for future in-place edits
- **Container-aware**: WAV and AIFF files store ID3v2 tags in their native chunk format (`id3 ` / `ID3 ` chunks), with correct RIFF/FORM size updates
- **ID3v2.4 output**: writes ID3v2.4 with UTF-8 encoding for maximum compatibility
- **ID3v2.3 + v2.4 input**: reads both versions, handling all text encodings (ISO-8859-1, UTF-16 LE/BE, UTF-8)
- **ID3v1 fallback**: reads ID3v1/v1.1 tags when no ID3v2 tag is present (MP3/AAC only)
- **No dependencies**: only requires POSIX + C11 stdlib
- **Clean builds**: compiles with `-Wall -Wextra -Wpedantic`

## Quick Start

### Simple: read/write a single tag

```c
#include <mp3tag/mp3tag.h>
#include <stdio.h>

int main(void) {
    mp3tag_context_t *ctx = mp3tag_create(NULL);

    /* Read a tag */
    mp3tag_open(ctx, "song.mp3");
    char title[256];
    if (mp3tag_read_tag_string(ctx, "TITLE", title, sizeof(title)) == MP3TAG_OK)
        printf("Title: %s\n", title);
    mp3tag_close(ctx);

    /* Write a tag */
    mp3tag_open_rw(ctx, "song.mp3");
    mp3tag_set_tag_string(ctx, "TITLE", "New Title");
    mp3tag_set_tag_string(ctx, "ARTIST", "New Artist");
    mp3tag_close(ctx);

    mp3tag_destroy(ctx);
    return 0;
}
```

### Full control: build a tag collection

```c
mp3tag_context_t *ctx = mp3tag_create(NULL);
mp3tag_open_rw(ctx, "song.mp3");

mp3tag_collection_t *coll = mp3tag_collection_create(ctx);
mp3tag_tag_t *tag = mp3tag_collection_add_tag(ctx, coll, MP3TAG_TARGET_ALBUM);

mp3tag_tag_add_simple(ctx, tag, "TITLE",    "Song Title");
mp3tag_tag_add_simple(ctx, tag, "ARTIST",   "Artist Name");
mp3tag_tag_add_simple(ctx, tag, "ALBUM",    "Album Name");
mp3tag_tag_add_simple(ctx, tag, "TRACK_NUMBER", "3");

mp3tag_simple_tag_t *comment = mp3tag_tag_add_simple(ctx, tag, "COMMENT", "A comment");
mp3tag_simple_tag_set_language(ctx, comment, "eng");

mp3tag_write_tags(ctx, coll);
mp3tag_collection_free(ctx, coll);
mp3tag_close(ctx);
mp3tag_destroy(ctx);
```

## Dependencies

- [libtag_common](https://github.com/morganp/libtag_common) — shared I/O, buffer, and string utilities (included as git submodule)

## Building

Clone with submodules:

```bash
git clone --recursive https://github.com/morganp/libmp3tag.git
```

If already cloned:

```bash
git submodule update --init
```

### XCFramework (macOS + iOS)

```bash
./build_xcframework.sh
# Output: build/xcframework/mp3tag.xcframework
```

### Manual build

```bash
xcrun clang -std=c11 -O2 -Wall -Iinclude -Ideps/libtag_common/include -c src/*.c src/**/*.c
ar rcs libmp3tag.a *.o
```

## Supported platforms

| Platform       | Architectures     | Min version |
|----------------|-------------------|-------------|
| macOS          | arm64, x86_64     | 10.15       |
| iOS            | arm64             | 13.0        |
| iOS Simulator  | arm64, x86_64     | 13.0        |

## API Reference

### Version & Error

| Function | Description |
|----------|-------------|
| `mp3tag_version()` | Returns version string ("1.0.0") |
| `mp3tag_strerror(int error)` | Human-readable error message |

### Context Lifecycle

| Function | Description |
|----------|-------------|
| `mp3tag_create(allocator)` | Create context (NULL = default malloc) |
| `mp3tag_destroy(ctx)` | Destroy context, close file |
| `mp3tag_open(ctx, path)` | Open file for reading |
| `mp3tag_open_rw(ctx, path)` | Open file for read/write |
| `mp3tag_close(ctx)` | Close file |
| `mp3tag_is_open(ctx)` | Check if a file is open |

### Tag Reading

| Function | Description |
|----------|-------------|
| `mp3tag_read_tags(ctx, &tags)` | Read all tags (context-owned) |
| `mp3tag_read_tag_string(ctx, name, buf, size)` | Read single tag by name |

### Tag Writing

| Function | Description |
|----------|-------------|
| `mp3tag_write_tags(ctx, tags)` | Replace all tags |
| `mp3tag_set_tag_string(ctx, name, value)` | Set/create single tag |
| `mp3tag_remove_tag(ctx, name)` | Remove a tag by name |

### Collection Building

| Function | Description |
|----------|-------------|
| `mp3tag_collection_create(ctx)` | Create empty collection |
| `mp3tag_collection_free(ctx, coll)` | Free collection |
| `mp3tag_collection_add_tag(ctx, coll, type)` | Add tag with target type |
| `mp3tag_tag_add_simple(ctx, tag, name, value)` | Add name/value pair |
| `mp3tag_simple_tag_add_nested(ctx, parent, name, value)` | Add nested child |
| `mp3tag_simple_tag_set_language(ctx, st, lang)` | Set language code |
| `mp3tag_tag_add_track_uid(ctx, tag, uid)` | Add track UID |

### Tag Name Mapping

| Name | ID3v2 Frame | Description |
|------|-------------|-------------|
| TITLE | TIT2 | Track title |
| ARTIST | TPE1 | Lead artist |
| ALBUM | TALB | Album name |
| ALBUM_ARTIST | TPE2 | Album artist |
| DATE_RELEASED | TDRC | Release date/year |
| TRACK_NUMBER | TRCK | Track number |
| DISC_NUMBER | TPOS | Disc number |
| GENRE | TCON | Genre |
| COMPOSER | TCOM | Composer |
| COMMENT | COMM | Comment |
| ENCODER | TSSE | Encoding software |
| COPYRIGHT | TCOP | Copyright |
| BPM | TBPM | Beats per minute |
| PUBLISHER | TPUB | Publisher |
| ISRC | TSRC | ISRC code |

Unknown tag names with 4 uppercase characters are used as raw frame IDs.
Other unknown names are stored as TXXX (user-defined text) frames.

## Supported Formats

| Format | Extension | Tag location | Notes |
|--------|-----------|-------------|-------|
| MP3    | .mp3      | Prepended ID3v2 at start of file | ID3v1 fallback at EOF |
| AAC    | .aac      | Prepended ID3v2 at start of file | ADTS streams |
| WAV    | .wav      | `id3 ` chunk in RIFF container | RIFF/WAVE size auto-updated |
| AIFF   | .aif/.aiff | `ID3 ` chunk in FORM container | FORM/AIFF size auto-updated |

The API is identical for all formats — the library auto-detects the container type on open.

## Project Structure

```
libmp3tag/
├── include/mp3tag/         # Public headers
│   ├── mp3tag.h            # Main API
│   ├── mp3tag_types.h      # Type definitions
│   ├── mp3tag_error.h      # Error codes
│   └── module.modulemap    # Swift/Clang module map
├── deps/
│   └── libtag_common/      # Shared I/O, buffer & string utilities (submodule)
├── src/
│   ├── mp3tag.c            # Main API implementation
│   ├── id3v2/              # ID3v2 format layer
│   │   ├── id3v2_defs.h    # Constants, frame ID mapping
│   │   ├── id3v2_reader.c  # ID3v2 parsing
│   │   └── id3v2_writer.c  # ID3v2 serialization
│   ├── id3v1/              # ID3v1 format layer
│   │   └── id3v1.c         # ID3v1 parsing (read-only)
│   └── container/          # Container format layer
│       └── container.c     # AIFF/WAV chunk detection & rewriting
└── tests/
    └── test_mp3tag.c       # Multi-format test suite (96 tests)
```

## License

MIT
