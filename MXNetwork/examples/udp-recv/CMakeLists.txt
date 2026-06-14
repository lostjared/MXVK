add_executable(mxnetwork-recv-udp ${CMAKE_SOURCE_DIR}/examples/udp-recv/recv.cpp)
target_link_libraries(mxnetwork-recv-udp PRIVATE libmxnetwork::mxnetwork)

