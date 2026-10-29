set(CLI_SOURCES main.cpp)
if(WIN32)
    list(APPEND CLI_SOURCES cli.rc)
endif()

add_executable(seicure ${CLI_SOURCES})

target_link_libraries(seicure PRIVATE seicure_job)

if(WIN32)
    # OBJECT_DEPENDS is required: the RC scanner does not follow ICON
    # references, so a regenerated icon would otherwise not trigger a rebuild.
    set_source_files_properties(cli.rc PROPERTIES
        COMPILE_FLAGS "-I\"${CMAKE_SOURCE_DIR}/assets\""
        OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/assets/seicure.ico")

    # wmain() instead of main(): argv arrives as UTF-16, so paths with
    # characters outside the active code page survive. GCC needs -municode at
    # both stages.
    if(NOT MSVC)
        target_compile_options(seicure PRIVATE -municode)
        target_link_options(seicure PRIVATE -municode)
    endif()
endif()

# Flat layout: the installer wants both executables side by side in one folder.
install(TARGETS seicure RUNTIME DESTINATION .)
