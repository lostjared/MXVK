 cmake_minimum_required(VERSION 3.10)
project(libmxvk VERSION 0.1.0)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands for tooling" FORCE)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(CheckIncludeFile)
include(CheckIncludeFileCXX)
include(CheckFunctionExists)
include(CheckLibraryExists)
include(CheckSymbolExists)
include(CheckCSourceCompiles)
include(CheckCXXSourceCompiles)
option(DEBUG_MODE "Debug mode" OFF)
option(VALIDATION "Enable Vulkan validation layers" OFF)
option(CV "Enable OpenCV video capture support (VK_Capture)" OFF)

if(VALIDATION)
add_compile_definitions(ENABLE_VALIDATION)
message(STATUS "Validation layers enabled")
else()
message(STATUS "Validation layers disabled")
endif()

if(DEBUG_MODE)
add_compile_definitions(DEBUG_MODE)
message(STATUS "Debug Enabled")
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
else()
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
message(STATUS "Release mode enabled")
endif()
if(UNIX AND NOT APPLE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
endif()
add_compile_options(-Wall)
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
if(MISSING_STD_HEADERS)
message(FATAL_ERROR "Missing required C++ standard headers: ${MISSING_STD_HEADERS}. "
"Please ensure you have a C++20 compliant compiler installed.")
endif()
message(STATUS "All required C++ standard headers found.")
check_include_file(stdlib.h HAVE_STDLIB_H)
check_include_file(stdio.h HAVE_STDIO_H)
check_include_file(stdint.h HAVE_STDINT_H)
check_include_file(string.h HAVE_STRING_H)
if(NOT HAVE_STDLIB_H OR NOT HAVE_STDIO_H OR NOT HAVE_STDINT_H)
message(FATAL_ERROR "Missing required C standard headers (stdlib.h, stdio.h, stdint.h)")
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
message(STATUS "")
message(STATUS "=== Checking External Dependencies ===")
find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
message(FATAL_ERROR "Missing dependency: SDL3. Install SDL3 development files and ensure CMake can find SDL3Config.cmake.")
endif()
if(DEFINED SDL3_VERSION)
message(STATUS "Found SDL3 ${SDL3_VERSION}")
elseif(DEFINED SDL3_VERSION_STRING)
message(STATUS "Found SDL3 ${SDL3_VERSION_STRING}")
else()
message(STATUS "Found SDL3")
endif()

find_package(Vulkan 1.4 QUIET)
if(NOT Vulkan_FOUND)
message(FATAL_ERROR "Missing dependency: Vulkan SDK 1.4 or newer. Install Vulkan loader/development files and ensure CMake can find VulkanConfig.cmake or vulkan headers/libs.")
endif()
if(DEFINED Vulkan_VERSION)
message(STATUS "Found Vulkan ${Vulkan_VERSION}")
else()
message(STATUS "Found Vulkan")
endif()
find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(SDL3_ttf REQUIRED)
find_package(glm REQUIRED)
find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()
if(CV)
    find_package(OpenCV REQUIRED)
    message(STATUS "OpenCV support enabled (version ${OpenCV_VERSION})")
else()
    message(STATUS "OpenCV support disabled (pass -DCV=ON to enable VK_Capture)")
endif()
add_subdirectory(volk)
add_library(mxvk STATIC
    mxvk/src/mxvk.cpp
    mxvk/src/mxvk_png.cpp
    mxvk/src/mxvk_sprite.cpp
    mxvk/src/mxvk_text.cpp
    mxvk/src/mxvk_model.cpp
    mxvk/src/mxvk_abstract_model.cpp
)

set(mxvk_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mxvk/shaders)
set(mxvk_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/mxvk/shaders)
set(mxvk_VERTEX_SHADER ${mxvk_SHADER_SOURCE_DIR}/sprite.vert)
set(mxvk_FRAGMENT_SHADER ${mxvk_SHADER_SOURCE_DIR}/sprite.frag)
set(mxvk_VERTEX_SPV ${mxvk_SHADER_OUTPUT_DIR}/sprite.vert.spv)
set(mxvk_FRAGMENT_SPV ${mxvk_SHADER_OUTPUT_DIR}/sprite.frag.spv)
set(mxvk_TEXT_VERTEX_SHADER ${mxvk_SHADER_SOURCE_DIR}/text.vert)
set(mxvk_TEXT_FRAGMENT_SHADER ${mxvk_SHADER_SOURCE_DIR}/text.frag)
set(mxvk_TEXT_VERTEX_SPV ${mxvk_SHADER_OUTPUT_DIR}/text.vert.spv)
set(mxvk_TEXT_FRAGMENT_SPV ${mxvk_SHADER_OUTPUT_DIR}/text.frag.spv)

add_custom_command(
    OUTPUT ${mxvk_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${mxvk_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${mxvk_VERTEX_SHADER} -o ${mxvk_VERTEX_SPV}
    DEPENDS ${mxvk_VERTEX_SHADER}
    COMMENT "Compiling MXVK sprite vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${mxvk_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${mxvk_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${mxvk_FRAGMENT_SHADER} -o ${mxvk_FRAGMENT_SPV}
    DEPENDS ${mxvk_FRAGMENT_SHADER}
    COMMENT "Compiling MXVK sprite fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${mxvk_TEXT_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${mxvk_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${mxvk_TEXT_VERTEX_SHADER} -o ${mxvk_TEXT_VERTEX_SPV}
    DEPENDS ${mxvk_TEXT_VERTEX_SHADER}
    COMMENT "Compiling MXVK text vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${mxvk_TEXT_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${mxvk_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${mxvk_TEXT_FRAGMENT_SHADER} -o ${mxvk_TEXT_FRAGMENT_SPV}
    DEPENDS ${mxvk_TEXT_FRAGMENT_SHADER}
    COMMENT "Compiling MXVK text fragment shader"
    VERBATIM
)

add_custom_target(mxvk_shaders
    DEPENDS
        ${mxvk_VERTEX_SPV}
        ${mxvk_FRAGMENT_SPV}
    ${mxvk_TEXT_VERTEX_SPV}
    ${mxvk_TEXT_FRAGMENT_SPV}
)
if(CV)
    target_sources(mxvk PRIVATE mxvk/src/mxvk_cv.cpp)
endif()
add_library(mxvk::mxvk ALIAS mxvk)
target_include_directories(mxvk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/mxvk/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if(DEFINED GLM_INCLUDE_DIRS)
    target_include_directories(mxvk PUBLIC ${GLM_INCLUDE_DIRS})
endif()
if(CV)
    target_include_directories(mxvk PUBLIC ${OpenCV_INCLUDE_DIRS})
    target_compile_definitions(mxvk PUBLIC MXVK_WITH_CV)
endif()
target_link_libraries(mxvk PUBLIC volk SDL3::SDL3 SDL3_ttf::SDL3_ttf Vulkan::Vulkan PNG::PNG ZLIB::ZLIB)
if(TARGET glm::glm)
    target_link_libraries(mxvk PUBLIC glm::glm)
endif()
add_dependencies(mxvk mxvk_shaders)
target_compile_definitions(mxvk PUBLIC
    MXVK_SPRITE_SHADER_DIR="${mxvk_SHADER_OUTPUT_DIR}"
    MXVK_TEXT_SHADER_DIR="${mxvk_SHADER_OUTPUT_DIR}"
)
if(CV)
    target_link_libraries(mxvk PUBLIC ${OpenCV_LIBS})
endif()
add_subdirectory(examples/hello_world)
add_subdirectory(examples/static_example)
add_subdirectory(examples/sprite_example)
add_subdirectory(examples/text_example)
add_subdirectory(examples/model_example)
add_subdirectory(examples/tux_example)
add_subdirectory(examples/asteroids)
add_subdirectory(examples/puzzle)
add_subdirectory(examples/pool_demo)
add_subdirectory(examples/fractal_zoom)
if(CV)
add_subdirectory(examples/opencv_example)
add_subdirectory(examples/opencv_model)
endif()

set(MXVK_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/mxvk")

install(TARGETS mxvk
    EXPORT mxvkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/mxvk/include/mxvk
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/mxvkConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mxvkConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/mxvkConfig.cmake
    INSTALL_DESTINATION ${MXVK_INSTALL_CMAKEDIR}
)

install(EXPORT mxvkTargets
    FILE mxvkTargets.cmake
    NAMESPACE mxvk::
    DESTINATION ${MXVK_INSTALL_CMAKEDIR}
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/mxvkConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/mxvkConfigVersion.cmake
    DESTINATION ${MXVK_INSTALL_CMAKEDIR}
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    IMMEDIATE
    @ONLY
)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
)
