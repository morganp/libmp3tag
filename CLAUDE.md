# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

Build library:
```sh
mkdir -p build && cd build && xcrun clang -c -std=c11 -Wall -Wextra -Wpedantic -Wno-unused-parameter -O2 -I ../include -I ../deps/libtag_common/include \
    ../src/mp3tag.c ../src/id3v2/id3v2_reader.c ../src/id3v2/id3v2_writer.c \
    ../src/id3v1/id3v1.c ../src/container/container.c \
    ../deps/libtag_common/src/file_io.c ../deps/libtag_common/src/buffer.c ../deps/libtag_common/src/string_util.c \
    && xcrun ar rcs libmp3tag.a mp3tag.o id3v2_reader.o id3v2_writer.o id3v1.o container.o file_io.o buffer.o string_util.o
```

Build XCFramework (macOS + iOS):
```sh
./build_xcframework.sh
```

## Architecture

Pure C11 static library for reading/writing ID3v2/ID3v1 tags in MP3, AAC, WAV, and AIFF files. No external dependencies (POSIX only). API is compatible with [libmkvtag](https://github.com/morganp/libmkvtag) and [liboggtag](https://github.com/morganp/liboggtag).

### Layers

- **Public API** (`include/mp3tag/`) — `mp3tag.h` (functions), `mp3tag_types.h` (structs/enums), `mp3tag_error.h` (error codes), `module.modulemap` (Swift/Clang)
- **Main implementation** (`src/mp3tag.c`) — Context lifecycle, format probing, tag read/write orchestration, collection building
- **ID3v2** (`src/id3v2/`) — ID3v2.3/v2.4 parsing (`id3v2_reader`), ID3v2.4 serialization (`id3v2_writer`), frame ID mapping and constants (`id3v2_defs.h`)
- **ID3v1** (`src/id3v1/`) — ID3v1/v1.1 parsing (read-only fallback for MP3/AAC)
- **Container** (`src/container/`) — AIFF/WAV chunk detection and rewriting (`container.c`)
- **Shared utilities** (`deps/libtag_common/`) — Buffered file I/O, dynamic byte buffer, string helpers (via libtag_common submodule)

### Supported Formats

| Format | Extension | Tag Location |
|--------|-----------|-------------|
| MP3 | .mp3 | Prepended ID3v2; ID3v1 fallback at EOF |
| AAC | .aac | Prepended ID3v2 (ADTS streams) |
| WAV | .wav | `id3 ` chunk in RIFF container |
| AIFF | .aif/.aiff | `ID3 ` chunk in FORM container |
| AVI | .avi | `id3 ` chunk in RIFF container |

### Write Strategy

- **In-place**: When new tags fit within existing ID3v2 space (including padding), updates in place
- **Safe rewrite**: When more space is needed, writes to temp file then atomic rename; adds 4KB padding for future in-place edits

### Tag Name Mapping

Canonical names map to ID3v2 frames: `TITLE`→`TIT2`, `ARTIST`→`TPE1`, `ALBUM`→`TALB`, `ALBUM_ARTIST`→`TPE2`, `DATE_RELEASED`→`TDRC`, `TRACK_NUMBER`→`TRCK`, `DISC_NUMBER`→`TPOS`, `GENRE`→`TCON`, `COMMENT`→`COMM`. Unknown 4-char names used as raw frame IDs; others stored as TXXX frames.
