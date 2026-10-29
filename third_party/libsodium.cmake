# libsodium vendored as a static library.
#
# Upstream builds with autotools, which we do not want as a prerequisite on
# three platforms. Compiling the sources directly works because libsodium
# guards every CPU-specific path behind feature macros that autotools would
# define; without them the portable C implementations are selected. Those are
# still constant-time -- ChaCha20 and Poly1305 are add/rotate/xor only -- so
# the fallback costs speed, never safety.

set(SODIUM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libsodium)

if(NOT EXISTS ${SODIUM_DIR}/src/libsodium)
    message(FATAL_ERROR "libsodium sources missing at ${SODIUM_DIR}")
endif()

# version.h is generated from a template by autoconf; CMake substitutes the
# same @VAR@ placeholders. It has to land beside the other sodium headers
# because it does #include "export.h" as a sibling -- which is also where
# upstream's own build puts it.
set(VERSION "1.0.22")
set(SODIUM_LIBRARY_VERSION_MAJOR 26)
set(SODIUM_LIBRARY_VERSION_MINOR 4)
set(SODIUM_LIBRARY_MINIMAL_DEF "")
configure_file(
    ${SODIUM_DIR}/src/libsodium/include/sodium/version.h.in
    ${SODIUM_DIR}/src/libsodium/include/sodium/version.h
    @ONLY)

file(GLOB_RECURSE SODIUM_SOURCES CONFIGURE_DEPENDS
     ${SODIUM_DIR}/src/libsodium/*.c)

add_library(sodium STATIC ${SODIUM_SOURCES})

target_include_directories(sodium
    PUBLIC
        ${SODIUM_DIR}/src/libsodium/include
    PRIVATE
        ${SODIUM_DIR}/src/libsodium/include/sodium)

# SODIUM_STATIC stops the headers from emitting dllimport declarations.
target_compile_definitions(sodium PUBLIC SODIUM_STATIC=1)
target_compile_definitions(sodium PRIVATE CONFIGURED=1)

if(NOT MSVC)
    target_compile_options(sodium PRIVATE -w)
endif()

if(WIN32)
    target_link_libraries(sodium PUBLIC advapi32)
endif()
