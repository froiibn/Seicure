cmake_minimum_required(VERSION 3.25)
project(Seicure VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(SEICURE_STATIC      "Link the runtime statically (single-file exe)" ON)
option(SEICURE_BUILD_TESTS "Build the test suite"                          ON)

# Windows and macOS have a graphical front end; Linux does not yet. The shared
# half of the GUI (src/gui/ui) builds everywhere regardless -- it is ordinary
# C++ with no windowing in it, and compiling it on every platform is what keeps
# it honest about staying portable.
if(WIN32 OR APPLE)
    option(SEICURE_BUILD_GUI "Build the graphical front end" ON)
else()
    set(SEICURE_BUILD_GUI OFF)
endif()

if(APPLE)
    enable_language(OBJCXX)
endif()

# Shared compile/link settings. Security hardening lives here so every target
# picks it up identically.
add_library(seicure_flags INTERFACE)

if(WIN32)
    target_compile_definitions(seicure_flags INTERFACE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

if(MSVC)
    target_compile_options(seicure_flags INTERFACE /W4 /permissive- /GS /guard:cf)
    target_link_options(seicure_flags INTERFACE
        /guard:cf /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)
else()
    target_compile_options(seicure_flags INTERFACE
        -Wall -Wextra -fstack-protector-strong)

    if(WIN32)
        # PE-specific hardening; these options mean nothing to an ELF linker.
        target_link_options(seicure_flags INTERFACE
            -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va
            -Wl,--no-insert-timestamp)
        if(SEICURE_STATIC)
            target_link_options(seicure_flags INTERFACE
                -static -static-libgcc -static-libstdc++)
        endif()
    elseif(APPLE)
        # Nothing to add here, and that is deliberate. The ELF options below
        # mean nothing to ld64, and macOS already does what they ask for:
        # executables are position independent by default, stacks are
        # non-executable, and chained fixups bind eagerly. There is also no
        # static option -- Apple ships no static libSystem and does not
        # support statically linked binaries -- so on this platform the
        # deployment target, not static linking, is what buys compatibility
        # with older systems.
        target_compile_options(seicure_flags INTERFACE -fPIE)
    else()
        target_compile_options(seicure_flags INTERFACE -fPIE)
        target_link_options(seicure_flags INTERFACE
            -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)

        if(SEICURE_STATIC)
            # Fully static, for the portable tarball. A dynamically linked
            # binary inherits the glibc version of whatever distro built it,
            # so a release built on 24.04 refuses to start on 22.04. Static
            # linking removes that floor entirely.
            #
            # Static glibc is normally discouraged because NSS resolves hosts
            # and users through dlopen'd modules that a static binary cannot
            # load. This program does neither -- no network, no passwd
            # lookups -- and the build emits no NSS warnings, so the objection
            # does not apply.
            target_link_options(seicure_flags INTERFACE -static)
        else()
            # Distribution packages link dynamically so that a glibc security
            # update reaches this binary too.
            target_link_options(seicure_flags INTERFACE -pie)
        endif()
    endif()
endif()

add_subdirectory(third_party)
add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/job)
add_subdirectory(src/cli)

add_subdirectory(src/gui)

if(SEICURE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
