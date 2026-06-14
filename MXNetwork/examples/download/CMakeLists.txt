add_executable(download-file ${CMAKE_SOURCE_DIR}/examples/download/dl-file.cpp)
target_link_libraries(download-file PRIVATE libmxnetwork::mxnetwork)

