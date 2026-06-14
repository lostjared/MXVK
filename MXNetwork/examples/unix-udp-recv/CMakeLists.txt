add_executable(mxnetwork-unix-recv-udp ${CMAKE_SOURCE_DIR}/examples/unix-udp-recv/recv.cpp)
target_link_libraries(mxnetwork-unix-recv-udp PRIVATE libmxnetwork::mxnetwork)

