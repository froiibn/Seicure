add_library(seicure_core STATIC
    secure_buffer.cpp
    crypto.cpp
    kdf.cpp
    format.cpp
    stream.cpp
    archive.cpp
)

# Headers are included as "core/...", so the include root is src/.
target_include_directories(seicure_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(seicure_core
    PUBLIC  seicure_flags
    PRIVATE sodium
)
