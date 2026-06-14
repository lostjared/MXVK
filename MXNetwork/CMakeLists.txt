cmake_minimum_required(VERSION 3.10)
project(libmxnetwork VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands for tooling" FORCE)
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(CheckIncludeFile)
include(CheckIncludeFileCXX)
include(CheckFunctionExists)
include(CheckLibraryExists)
include(CheckSymbolExists)
include(CheckCSourceCompiles)
include(CheckCXXSourceCompiles)

option(CLIENT "Qt6 relay client" OFF)
option(MXNETWORK_EXAMPLES "Examples ON/OFF" ON)

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "DEBUG")
add_compile_definitions(DEBUG_MODE)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
if (CMAKE_C_COMPILER_ID STREQUAL "GNU")
    message(STATUS "using GNU GCC")
    message(STATUS "Enabled: Strict warnings.")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Warray-bounds=2
        -Wshadow
        -Wconversion
    )

    if(NOT CLIENT)
        message(STATUS "Debug Enabled")
        add_compile_options(
            "$<$<CONFIG:Debug>:-fsanitize=address>"
            "$<$<CONFIG:Debug>:-fno-omit-frame-pointer>"
        )
        add_link_options(
            "$<$<CONFIG:Debug>:-fsanitize=address>"
        )
    endif()
endif()
else()
    set(CMAKE_BUILD_TYPE Release)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
    message(STATUS "Release mode enabled")
    add_compile_options(-Wall -pedantic)
endif()
if(UNIX AND NOT APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
    add_compile_definitions($<$<COMPILE_LANGUAGE:C>:_POSIX_C_SOURCE=200809L>)
endif()


set(CMAKE_REQUIRED_FLAGS "-std=c++20")
message(STATUS "")
message(STATUS "=== Checking System Headers ===")
check_include_file_cxx(iostream HAVE_IOSTREAM)
check_include_file_cxx(string HAVE_STRING)
check_include_file_cxx(vector HAVE_VECTOR)
check_include_file_cxx(memory HAVE_MEMORY)
check_include_file_cxx(fstream HAVE_FSTREAM)
check_include_file_cxx(sstream HAVE_SSTREAM)
check_include_file_cxx(optional HAVE_OPTIONAL)
check_include_file_cxx(thread HAVE_THREAD)
check_include_file_cxx(mutex HAVE_MUTEX)
check_include_file_cxx(atomic HAVE_ATOMIC)
check_include_file_cxx(functional HAVE_FUNCTIONAL)
check_include_file_cxx(unordered_map HAVE_UNORDERED_MAP)
check_include_file_cxx(queue HAVE_QUEUE)
check_include_file_cxx(condition_variable HAVE_CONDITION_VARIABLE)
check_include_file_cxx(set HAVE_SET)
check_include_file_cxx(array HAVE_ARRAY)
check_include_file_cxx(algorithm HAVE_ALGORITHM)
check_include_file_cxx(stdexcept HAVE_STDEXCEPT)
check_include_file_cxx(string_view HAVE_STRING_VIEW)

set(MISSING_STD_HEADERS "")
if(NOT HAVE_IOSTREAM)
list(APPEND MISSING_STD_HEADERS "iostream")
endif()
if(NOT HAVE_STRING)
list(APPEND MISSING_STD_HEADERS "string")
endif()
if(NOT HAVE_VECTOR)
list(APPEND MISSING_STD_HEADERS "vector")
endif()
if(NOT HAVE_MEMORY)
list(APPEND MISSING_STD_HEADERS "memory")
endif()
if(NOT HAVE_FSTREAM)
list(APPEND MISSING_STD_HEADERS "fstream")
endif()
if(NOT HAVE_OPTIONAL)
list(APPEND MISSING_STD_HEADERS "optional (C++17 required)")
endif()
if(NOT HAVE_THREAD)
list(APPEND MISSING_STD_HEADERS "thread")
endif()
if(NOT HAVE_MUTEX)
list(APPEND MISSING_STD_HEADERS "mutex")
endif()
if(NOT HAVE_ATOMIC)
list(APPEND MISSING_STD_HEADERS "atomic")
endif()
if(NOT HAVE_STRING_VIEW)
list(APPEND MISSING_STD_HEADERS "string_view")
endif()
if(MISSING_STD_HEADERS)
message(FATAL_ERROR "Missing required C++ standard headers: ${MISSING_STD_HEADERS}. "
"Please ensure you have a C++20 compliant compiler installed.")
endif()
message(STATUS "All required C++ standard headers found.")

check_include_file(stdlib.h HAVE_STDLIB_H)
check_include_file(stdio.h HAVE_STDIO_H)
check_include_file(stdint.h HAVE_STDINT_H)
check_include_file(string.h HAVE_STRING_H)
if(UNIX)
check_include_file(sys/socket.h HAVE_SOCKET_H)
check_symbol_exists(MSG_DONTWAIT "sys/socket.h" HAVE_MSG_DONTWAIT)
check_include_file(sys/un.h HAVE_UN_H)
check_include_file(unistd.h HAVE_UNISTD_H)
check_include_file(arpa/inet.h HAVE_ARPA_H)
check_include_file(fcntl.h HAVE_FCNTL_H)
check_include_file(netdb.h HAVE_NETDB_H)
check_include_file(sys/stat.h HAVE_STAT_H)
check_include_file(netinet/in.h HAVE_INET_H)
check_include_file(stdbool.h HAVE_BOOL_H)
check_include_file(signal.h HAVE_SIGNAL_H)
check_include_file(errno.h HAVE_ERRNO_H)
check_include_file(poll.h HAVE_POLL_H)
if(NOT HAVE_STDLIB_H OR NOT HAVE_STDIO_H OR NOT HAVE_STDINT_H OR NOT HAVE_SOCKET_H OR NOT HAVE_UN_H OR NOT HAVE_UNISTD_H OR NOT HAVE_ARPA_H OR NOT HAVE_FCNTL_H OR NOT HAVE_NETDB_H OR NOT HAVE_STAT_H OR NOT HAVE_INET_H OR NOT HAVE_BOOL_H OR NOT HAVE_SIGNAL_H OR NOT HAVE_ERRNO_H OR NOT HAVE_POLL_H)
message(FATAL_ERROR "Missing required C headers ")
endif()
else()
check_include_file(windows.h HAVE_WINDOWS_H)
check_include_file(winsock2.h HAVE_WSOCK32_H)
check_include_file(ws2tcpip.h HAVE_WS2TCPIP_H)
check_include_file(afunix.h HAVE_AFUNIX_H)
if(NOT HAVE_STDLIB_H OR NOT HAVE_STDIO_H OR NOT HAVE_STDINT_H OR NOT HAVE_WINDOWS_H OR NOT HAVE_WSOCK32_H OR NOT HAVE_WS2TCPIP_H)
   message(FATAL_ERROR "Missing required C headers for Windows")
endif()
endif()
message(STATUS "All required C standard headers found.")
message(STATUS "")
message(STATUS "=== Checking C++20 Compatibility ===")
check_cxx_source_compiles("
#include <version>
#if __cplusplus < 202002L
#error C++20 required
#endif
int main() { return 0; }
" HAVE_CXX20)
if(NOT HAVE_CXX20)
message(WARNING "C++20 support may not be fully available. Some features might not compile correctly.")
else()
message(STATUS "C++20 support confirmed.")
endif()
unset(CMAKE_REQUIRED_FLAGS)
message(STATUS "")
find_package(Threads REQUIRED)
add_library(mxnetwork STATIC src/mxsocket.c src/socket.cpp src/exception.cpp)
if(HAVE_MSG_DONTWAIT)
    target_compile_definitions(mxnetwork PRIVATE $<$<COMPILE_LANGUAGE:C>:MX_HAVE_MSG_DONTWAIT>)
endif()
target_link_libraries(mxnetwork PUBLIC Threads::Threads)
target_include_directories(mxnetwork PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
add_library(libmxnetwork::mxnetwork ALIAS mxnetwork)
if(WIN32)
    target_link_libraries(mxnetwork PUBLIC ws2_32)
endif()
if(MXNETWORK_EXAMPLES)
    add_subdirectory(examples/client)
    add_subdirectory(examples/fileserve)
    add_subdirectory(examples/server)
    add_subdirectory(examples/udp-recv)
    add_subdirectory(examples/udp-send)
    add_subdirectory(examples/unix-udp-recv)
    add_subdirectory(examples/unix-udp-send)
if(UNIX)
    add_subdirectory(examples/download)
    add_subdirectory(examples/relay)
endif()
    if(CLIENT)
        message(STATUS "Qt6 client example")
        add_subdirectory(examples/relay-client)
    endif()
endif()

set(MXNETWORK_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/mxnetwork")

install(TARGETS mxnetwork
    EXPORT mxnetworkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/mxnetwork
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/mxnetworkConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mxnetworkConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/mxnetworkConfig.cmake
    INSTALL_DESTINATION ${MXNETWORK_INSTALL_CMAKEDIR}
)

install(EXPORT mxnetworkTargets
    FILE mxnetworkTargets.cmake
    NAMESPACE mxnetwork::
    DESTINATION ${MXNETWORK_INSTALL_CMAKEDIR}
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/mxnetworkConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/mxnetworkConfigVersion.cmake
    DESTINATION ${MXNETWORK_INSTALL_CMAKEDIR}
)
