cmake_minimum_required(VERSION 3.15)
project(mp3tag VERSION 1.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------- Sources ----------
set(MP3TAG_SOURCES
    src/mp3tag.c
    src/id3v2/id3v2_reader.c
    src/id3v2/id3v2_writer.c
    src/id3v1/id3v1.c
    src/container/container.c
    src/io/file_io.c
    src/util/buffer.c
    src/util/string_util.c
)

add_library(mp3tag STATIC ${MP3TAG_SOURCES})

target_include_directories(mp3tag
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Strict warnings
target_compile_options(mp3tag PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
)

# ---------- Apple platform settings ----------
if(APPLE)
    set_target_properties(mp3tag PROPERTIES
        XCODE_ATTRIBUTE_CLANG_ENABLE_MODULES YES
    )
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum iOS version")
    endif()
endif()

# ---------- Install ----------
include(GNUInstallDirs)

install(TARGETS mp3tag
    EXPORT mp3tagTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/mp3tag
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT mp3tagTargets
    FILE mp3tagTargets.cmake
    NAMESPACE mp3tag::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mp3tag
)

# ---------- Tests (optional) ----------
option(MP3TAG_BUILD_TESTS "Build tests" OFF)
if(MP3TAG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
