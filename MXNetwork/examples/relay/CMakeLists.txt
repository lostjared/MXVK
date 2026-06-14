find_package(Threads REQUIRED)
add_executable(mxnetwork-relay ${CMAKE_SOURCE_DIR}/examples/relay/relay.cpp)
target_link_libraries(mxnetwork-relay PRIVATE libmxnetwork::mxnetwork Threads::Threads)

