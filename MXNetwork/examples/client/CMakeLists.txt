add_executable(mxnetwork-client ${CMAKE_SOURCE_DIR}/examples/client/client.cpp)
target_link_libraries(mxnetwork-client PRIVATE libmxnetwork::mxnetwork)

