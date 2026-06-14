add_executable(mxnetwork-unix-send-udp ${CMAKE_SOURCE_DIR}/examples/unix-udp-send/send.cpp)
target_link_libraries(mxnetwork-unix-send-udp PRIVATE libmxnetwork::mxnetwork)
