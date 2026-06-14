add_executable(mxnetwork-fileserve ${CMAKE_SOURCE_DIR}/examples/fileserve/fileserve.cpp)
target_link_libraries(mxnetwork-fileserve PRIVATE libmxnetwork::mxnetwork)
