# The platform layer is the only place that talks to the OS. Everything above
# it -- core, job, cli -- is portable, so a port means adding a file here
# rather than touching anything else.
if(WIN32)
    set(PLATFORM_SOURCES file_win32.cpp console_win32.cpp)
elseif(UNIX)
    set(PLATFORM_SOURCES file_posix.cpp console_posix.cpp)
else()
    message(FATAL_ERROR "No platform implementation for this system")
endif()

add_library(seicure_platform STATIC ${PLATFORM_SOURCES})

target_include_directories(seicure_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(seicure_platform
    PUBLIC  seicure_core
)
