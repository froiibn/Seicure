add_library(seicure_job STATIC job.cpp)

target_include_directories(seicure_job PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(seicure_job PUBLIC seicure_platform)
