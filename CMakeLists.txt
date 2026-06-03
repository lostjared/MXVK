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
option(MIXER "Enable SDL3_mixer audio support" OFF)
option(JPEG "Enable JPEG image support" OFF)
option(EXAMPLES "Enable/Disable Examples" ON)
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
add_compile_options(-Wall -pedantic)
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

set(MXVK_USE_MOLTENVK OFF)
set(MXVK_MOLTENVK_LINK_TARGET "")
if(APPLE)
    option(MXVK_ENABLE_MOLTENVK "Automatically enable MoltenVK support on Apple platforms when available" ON)
    if(MXVK_ENABLE_MOLTENVK)
        if(TARGET Vulkan::MoltenVK)
            set(MXVK_USE_MOLTENVK ON)
            set(MXVK_MOLTENVK_LINK_TARGET Vulkan::MoltenVK)
        elseif(DEFINED Vulkan_MoltenVK_LIBRARY AND EXISTS "${Vulkan_MoltenVK_LIBRARY}")
            set(MXVK_USE_MOLTENVK ON)
            set(MXVK_MOLTENVK_LINK_TARGET "${Vulkan_MoltenVK_LIBRARY}")
        else()
            find_library(MXVK_MOLTENVK_LIBRARY
                NAMES MoltenVK
                HINTS
                    "$ENV{VULKAN_SDK}/lib"
                    "$ENV{VULKAN_SDK}/macOS/lib"
                    "/opt/homebrew/lib"
                    "/usr/local/lib"
            )
            if(MXVK_MOLTENVK_LIBRARY)
                set(MXVK_USE_MOLTENVK ON)
                set(MXVK_MOLTENVK_LINK_TARGET "${MXVK_MOLTENVK_LIBRARY}")
            endif()
        endif()

        if(MXVK_USE_MOLTENVK)
            message(STATUS "MoltenVK support enabled")
        else()
            message(STATUS "MoltenVK support not detected; using standard Vulkan loader path")
        endif()
    else()
        message(STATUS "MoltenVK support disabled by MXVK_ENABLE_MOLTENVK=OFF")
    endif()
endif()
find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(SDL3_ttf REQUIRED)
find_package(glm REQUIRED)
if(MIXER)
    find_package(SDL3_mixer REQUIRED)
    message(STATUS "SDL3_mixer support enabled")
else()
    message(STATUS "SDL3_mixer support disabled (pass -DMIXER=ON to enable)")
endif()
if(JPEG)
    find_package(JPEG REQUIRED)
    message(STATUS "JPEG support enabled")
else()
    message(STATUS "JPEG support disabled (pass -DJPEG=ON to enable)")
endif()
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
    mxvk/src/mxvk_console.cpp
    mxvk/src/mxvk_controller.cpp
    mxvk/src/mxvk_png.cpp
    mxvk/src/mxvk_sprite.cpp
    mxvk/src/mxvk_text.cpp
    mxvk/src/mxvk_model.cpp
    mxvk/src/mxvk_abstract_model.cpp
    mxvk/src/mxvk_cfg.cpp
    mxvk/src/mxvk_io_window.cpp
)
if(MIXER)
    target_sources(mxvk PRIVATE mxvk/src/mxvk_sound.cpp)
endif()
if(JPEG)
    target_sources(mxvk PRIVATE mxvk/src/mxvk_jpeg.cpp)
endif()

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
if(MIXER)
    target_compile_definitions(mxvk PUBLIC MXVK_WITH_MIXER WITH_MIXER)
endif()
if(JPEG)
    target_compile_definitions(mxvk PUBLIC MXVK_WITH_JPEG WITH_JPEG)
endif()
if(MXVK_USE_MOLTENVK)
    target_compile_definitions(mxvk PUBLIC MXVK_USE_MOLTENVK VK_ENABLE_BETA_EXTENSIONS)
endif()
target_link_libraries(mxvk PUBLIC volk SDL3::SDL3 SDL3_ttf::SDL3_ttf Vulkan::Vulkan PNG::PNG ZLIB::ZLIB)
if(MXVK_USE_MOLTENVK)
    target_link_libraries(mxvk PUBLIC ${MXVK_MOLTENVK_LINK_TARGET})
    target_link_libraries(mxvk PUBLIC "-framework Metal" "-framework QuartzCore" "-framework Cocoa" "-framework Foundation" "-framework IOKit")
endif()
if(MIXER)
    target_link_libraries(mxvk PUBLIC SDL3_mixer::SDL3_mixer)
endif()
if(JPEG)
    target_link_libraries(mxvk PUBLIC JPEG::JPEG)
endif()
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
if(EXAMPLES)
    message(STATUS "Examples ON")
    add_subdirectory(examples/hello_world)
    add_subdirectory(examples/static_example)
    add_subdirectory(examples/sprite_example)
    add_subdirectory(examples/text_example)
    add_subdirectory(examples/model_example)
    add_subdirectory(examples/tux_example)
    add_subdirectory(examples/glitch_cube)
    add_subdirectory(examples/asteroids)
    add_subdirectory(examples/pong)
    add_subdirectory(examples/puzzle)
    add_subdirectory(examples/pool_demo)
    add_subdirectory(examples/fractal_zoom)
    add_subdirectory(examples/console_demo)
    add_subdirectory(examples/walk)
    if(CV)
        add_subdirectory(examples/compute_shader)
        add_subdirectory(examples/opencv_example)
        add_subdirectory(examples/opencv_model)
    endif()

    function(mxvk_copy_runtime_shaders target_name)
        if(NOT TARGET ${target_name})
            return()
        endif()

        string(MAKE_C_IDENTIFIER "${target_name}" target_name_id)
        set(copy_target "mxvk_copy_runtime_shaders_${target_name_id}")

        add_custom_target(${copy_target} ALL
            COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target_name}>/data
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${mxvk_VERTEX_SPV}
                $<TARGET_FILE_DIR:${target_name}>/data/sprite.vert.spv
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${mxvk_FRAGMENT_SPV}
                $<TARGET_FILE_DIR:${target_name}>/data/sprite.frag.spv
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${mxvk_TEXT_VERTEX_SPV}
                $<TARGET_FILE_DIR:${target_name}>/data/text.vert.spv
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${mxvk_TEXT_FRAGMENT_SPV}
                $<TARGET_FILE_DIR:${target_name}>/data/text.frag.spv
            COMMENT "Copying MXVK sprite/text runtime shaders to ${target_name} data directory"
            VERBATIM
        )

        add_dependencies(${copy_target} mxvk_shaders)
    endfunction()

    set(mxvk_example_targets
        hello_world
        static_example
        sprite_example
        text_example
        model_example
        tux_example
        glitch_cube
        asteroids
        pong
        puzzle
        Pool3D
        fractal_zoom
        console_demo
    )
    if(CV)
        list(APPEND mxvk_example_targets opencv_example opencv_model)
    endif()

    foreach(example_target IN LISTS mxvk_example_targets)
        mxvk_copy_runtime_shaders(${example_target})
    endforeach()
else()
    message(STATUS "Examples OFF")
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
