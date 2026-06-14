add_executable(mxnetwork-send-udp ${CMAKE_SOURCE_DIR}/examples/udp-send/send.cpp)
target_link_libraries(mxnetwork-send-udp PRIVATE libmxnetwork::mxnetwork)

