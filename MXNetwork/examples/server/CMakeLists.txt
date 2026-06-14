find_package(Threads REQUIRED)
add_executable(mxnetwork-server ${CMAKE_SOURCE_DIR}/examples/server/server.cpp)
target_link_libraries(mxnetwork-server PRIVATE libmxnetwork::mxnetwork Threads::Threads)

