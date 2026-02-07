#!/bin/bash
# SPDX-License-Identifier: MIT
# Build mp3tag as an XCFramework for Apple platforms.
#
# Usage:
#   ./build_xcframework.sh [--output DIR]
#
# Produces: mp3tag.xcframework/

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUTPUT_DIR="${SCRIPT_DIR}/build/xcframework"
BUILD_DIR="${SCRIPT_DIR}/build"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --output) OUTPUT_DIR="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# Source files
SOURCES=(
    src/mp3tag.c
    src/id3v2/id3v2_reader.c
    src/id3v2/id3v2_writer.c
    src/id3v1/id3v1.c
    src/container/container.c
    src/io/file_io.c
    src/util/buffer.c
    src/util/string_util.c
)

CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -Wno-unused-parameter -O2 -I${SCRIPT_DIR}/include"

# --- Helper: build a static library for one platform/arch ---
build_platform() {
    local platform="$1"
    local arch="$2"
    local sdk="$3"
    local min_version_flag="$4"
    local out_dir="${BUILD_DIR}/${platform}-${arch}"

    echo "Building ${platform} ${arch}..."
    mkdir -p "${out_dir}"

    local objects=()
    for src in "${SOURCES[@]}"; do
        local obj="${out_dir}/$(basename "${src}" .c).o"
        xcrun --sdk "${sdk}" clang ${CFLAGS} ${min_version_flag} \
            -arch "${arch}" -c "${SCRIPT_DIR}/${src}" -o "${obj}"
        objects+=("${obj}")
    done

    xcrun --sdk "${sdk}" ar rcs "${out_dir}/libmp3tag.a" "${objects[@]}"

    # Copy headers
    mkdir -p "${out_dir}/include"
    cp -R "${SCRIPT_DIR}/include/mp3tag" "${out_dir}/include/"
}

# --- Build all platforms ---

# macOS
build_platform "macos" "arm64"  "macosx" "-mmacosx-version-min=10.15"
build_platform "macos" "x86_64" "macosx" "-mmacosx-version-min=10.15"

# iOS device
build_platform "ios" "arm64" "iphoneos" "-miphoneos-version-min=13.0"

# iOS Simulator
build_platform "iossim" "arm64"  "iphonesimulator" "-mios-simulator-version-min=13.0"
build_platform "iossim" "x86_64" "iphonesimulator" "-mios-simulator-version-min=13.0"

# --- Create fat libraries ---
echo "Creating fat libraries..."

# macOS fat
mkdir -p "${BUILD_DIR}/macos-fat"
xcrun lipo -create \
    "${BUILD_DIR}/macos-arm64/libmp3tag.a" \
    "${BUILD_DIR}/macos-x86_64/libmp3tag.a" \
    -output "${BUILD_DIR}/macos-fat/libmp3tag.a"
cp -R "${BUILD_DIR}/macos-arm64/include" "${BUILD_DIR}/macos-fat/"

# iOS Simulator fat
mkdir -p "${BUILD_DIR}/iossim-fat"
xcrun lipo -create \
    "${BUILD_DIR}/iossim-arm64/libmp3tag.a" \
    "${BUILD_DIR}/iossim-x86_64/libmp3tag.a" \
    -output "${BUILD_DIR}/iossim-fat/libmp3tag.a"
cp -R "${BUILD_DIR}/iossim-arm64/include" "${BUILD_DIR}/iossim-fat/"

# --- Create XCFramework ---
echo "Creating XCFramework..."
rm -rf "${OUTPUT_DIR}/mp3tag.xcframework"
mkdir -p "${OUTPUT_DIR}"

xcodebuild -create-xcframework \
    -library "${BUILD_DIR}/macos-fat/libmp3tag.a" \
    -headers "${BUILD_DIR}/macos-fat/include" \
    -library "${BUILD_DIR}/ios-arm64/libmp3tag.a" \
    -headers "${BUILD_DIR}/ios-arm64/include" \
    -library "${BUILD_DIR}/iossim-fat/libmp3tag.a" \
    -headers "${BUILD_DIR}/iossim-fat/include" \
    -output "${OUTPUT_DIR}/mp3tag.xcframework"

echo ""
echo "Done! XCFramework created at:"
echo "  ${OUTPUT_DIR}/mp3tag.xcframework"
