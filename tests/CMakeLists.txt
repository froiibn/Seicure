add_executable(seicure_tests
    main.cpp
    test_crypto.cpp
    test_kdf.cpp
    test_format.cpp
    test_stream.cpp
    test_archive.cpp
    test_job.cpp
)

target_include_directories(seicure_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(seicure_tests PRIVATE seicure_core seicure_job sodium)

add_test(NAME seicure_tests COMMAND seicure_tests)
