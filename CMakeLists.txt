cmake_minimum_required(VERSION 3.10)
project(libmxvk VERSION 0.3.1)
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
set(WITH_CUDA "AUTO" CACHE STRING "Enable CUDA acceleration/interop (AUTO, ON, OFF)")
set_property(CACHE WITH_CUDA PROPERTY STRINGS AUTO ON OFF)
set(WITH_MIXER "AUTO" CACHE STRING "Enable SDL3_mixer audio support (AUTO, ON, OFF)")
set_property(CACHE WITH_MIXER PROPERTY STRINGS AUTO ON OFF)
option(JPEG "Enable JPEG image support" OFF)
option(EXAMPLES "Enable/Disable Examples" ON)

if(MINGW AND CMAKE_CROSSCOMPILING)
set(MXVK_MINGW_TRIPLET "")
if(DEFINED CMAKE_CXX_COMPILER_TARGET AND CMAKE_CXX_COMPILER_TARGET MATCHES "w64-mingw32")
set(MXVK_MINGW_TRIPLET "${CMAKE_CXX_COMPILER_TARGET}")
elseif(DEFINED CMAKE_C_COMPILER_TARGET AND CMAKE_C_COMPILER_TARGET MATCHES "w64-mingw32")
set(MXVK_MINGW_TRIPLET "${CMAKE_C_COMPILER_TARGET}")
else()
set(MXVK_MINGW_TRIPLET "x86_64-w64-mingw32")
endif()

set(MXVK_MINGW_PREFIX "/usr/${MXVK_MINGW_TRIPLET}")
if(EXISTS "${MXVK_MINGW_PREFIX}")
list(PREPEND CMAKE_PREFIX_PATH "${MXVK_MINGW_PREFIX}")
list(PREPEND CMAKE_FIND_ROOT_PATH "${MXVK_MINGW_PREFIX}")
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)

if(EXISTS "${MXVK_MINGW_PREFIX}/lib/cmake/SDL3")
set(SDL3_DIR "${MXVK_MINGW_PREFIX}/lib/cmake/SDL3" CACHE PATH "SDL3 config directory for MinGW cross builds" FORCE)
endif()
if(EXISTS "${MXVK_MINGW_PREFIX}/lib/cmake/SDL3_ttf")
set(SDL3_ttf_DIR "${MXVK_MINGW_PREFIX}/lib/cmake/SDL3_ttf" CACHE PATH "SDL3_ttf config directory for MinGW cross builds" FORCE)
endif()
if(EXISTS "${MXVK_MINGW_PREFIX}/lib/cmake/SDL3_mixer")
set(SDL3_mixer_DIR "${MXVK_MINGW_PREFIX}/lib/cmake/SDL3_mixer" CACHE PATH "SDL3_mixer config directory for MinGW cross builds" FORCE)
endif()

if(EXISTS "${MXVK_MINGW_PREFIX}/lib/cmake/Vulkan")
set(Vulkan_DIR "${MXVK_MINGW_PREFIX}/lib/cmake/Vulkan" CACHE PATH "Vulkan config directory for MinGW cross builds" FORCE)
endif()
if(EXISTS "${MXVK_MINGW_PREFIX}/include/vulkan/vulkan.h")
set(Vulkan_INCLUDE_DIR "${MXVK_MINGW_PREFIX}/include" CACHE PATH "Vulkan include directory for MinGW cross builds" FORCE)
endif()
find_library(MXVK_MINGW_VULKAN_LIBRARY
NAMES vulkan-1 vulkan
PATHS "${MXVK_MINGW_PREFIX}/lib"
NO_DEFAULT_PATH
)
if(MXVK_MINGW_VULKAN_LIBRARY)
set(Vulkan_LIBRARY "${MXVK_MINGW_VULKAN_LIBRARY}" CACHE FILEPATH "Vulkan library for MinGW cross builds" FORCE)
endif()

message(STATUS "MinGW cross-compile: preferring packages from ${MXVK_MINGW_PREFIX}")
else()
message(WARNING "MinGW cross-compile detected but prefix ${MXVK_MINGW_PREFIX} was not found; package discovery may fall back to host /usr paths")
endif()
endif()

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
find_package(Threads REQUIRED)
string(TOUPPER "${WITH_MIXER}" MXVK_WITH_MIXER_REQUEST)
if(NOT MXVK_WITH_MIXER_REQUEST MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "WITH_MIXER must be AUTO, ON, or OFF")
endif()
string(TOUPPER "${WITH_CUDA}" MXVK_WITH_CUDA_REQUEST)
if(NOT MXVK_WITH_CUDA_REQUEST MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "WITH_CUDA must be AUTO, ON, or OFF")
endif()

set(MIXER OFF)
if(NOT MXVK_WITH_MIXER_REQUEST STREQUAL "OFF")
    find_package(SDL3_mixer QUIET)
    if(SDL3_mixer_FOUND)
        message(STATUS "SDL3_mixer support enabled")
        set(MIXER ON)
    else()
        if(MXVK_WITH_MIXER_REQUEST STREQUAL "ON")
            message(FATAL_ERROR "WITH_MIXER=ON was requested, but SDL3_mixer was not found")
        endif()
        message(STATUS "SDL3_mixer support not detected; building without audio support")
    endif()
else()
    message(STATUS "SDL3_mixer support disabled by WITH_MIXER=OFF")
endif()

set(MXVK_CUDA_ENABLED OFF)
set(MXVK_CUDA_USE_CUDAToolkit OFF)
if(NOT MXVK_WITH_CUDA_REQUEST STREQUAL "OFF")
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        message(STATUS "CUDA support enabled (CUDAToolkit ${CUDAToolkit_VERSION})")
        set(MXVK_CUDA_ENABLED ON)
        set(MXVK_CUDA_USE_CUDAToolkit ON)
    else()
        find_package(CUDA QUIET)
        if(CUDA_FOUND)
            message(STATUS "CUDA support enabled (version ${CUDA_VERSION_STRING})")
            set(MXVK_CUDA_ENABLED ON)
            set(MXVK_CUDA_USE_CUDAToolkit OFF)
        else()
            if(MXVK_WITH_CUDA_REQUEST STREQUAL "ON")
                message(FATAL_ERROR "WITH_CUDA=ON was requested, but no CUDA toolkit was found")
            endif()
            message(STATUS "CUDA support not detected; building without CUDA acceleration")
        endif()
    endif()
else()
    message(STATUS "CUDA support disabled by WITH_CUDA=OFF")
endif()

if(CV OR MXVK_CUDA_ENABLED)
    if(CV OR MXVK_WITH_CUDA_REQUEST STREQUAL "ON")
        find_package(OpenCV REQUIRED)
    else()
        find_package(OpenCV QUIET)
    endif()

    if(OpenCV_FOUND)
        if(CV)
            message(STATUS "OpenCV support enabled (version ${OpenCV_VERSION})")
        else()
            message(STATUS "OpenCV found for CUDA GpuMat interop (version ${OpenCV_VERSION})")
        endif()
    elseif(MXVK_CUDA_ENABLED)
        message(STATUS "CUDA support disabled: OpenCV not found for cv::cuda::GpuMat interop")
        set(MXVK_CUDA_ENABLED OFF)
        set(MXVK_CUDA_USE_CUDAToolkit OFF)
    endif()
endif()

if(NOT CV)
    message(STATUS "OpenCV capture support disabled (pass -DCV=ON to enable VK_Capture)")
endif()
add_subdirectory(volk)
set(MXNETWORK_EXAMPLES OFF CACHE BOOL "MXNetwork examples ON/OFF" FORCE)
add_subdirectory(MXNetwork)

add_library(mxvk STATIC
    mxvk/src/mxvk.cpp
    mxvk/src/mxvk_console.cpp
    mxvk/src/mxvk_controller.cpp
    mxvk/src/mxvk_png.cpp
    mxvk/src/mxvk_sprite.cpp
    mxvk/src/mxvk_sprite3d.cpp
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

set(mxvk_VERSION_HEADER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/mxvk_version.hpp.in)
set(mxvk_VERSION_HEADER_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/mxvk/include/mxvk/mxvk_version.hpp)
configure_file(${mxvk_VERSION_HEADER_SOURCE} ${mxvk_VERSION_HEADER_OUTPUT} @ONLY)

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
set(mxvk_SPRITE3D_VERTEX_SHADER ${mxvk_SHADER_SOURCE_DIR}/sprite3d.vert)
set(mxvk_SPRITE3D_FRAGMENT_SHADER ${mxvk_SHADER_SOURCE_DIR}/sprite3d.frag)
set(mxvk_SPRITE3D_VERTEX_SPV ${mxvk_SHADER_OUTPUT_DIR}/sprite3d.vert.spv)
set(mxvk_SPRITE3D_FRAGMENT_SPV ${mxvk_SHADER_OUTPUT_DIR}/sprite3d.frag.spv)

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

add_custom_command(
    OUTPUT ${mxvk_SPRITE3D_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${mxvk_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${mxvk_SPRITE3D_VERTEX_SHADER} -o ${mxvk_SPRITE3D_VERTEX_SPV}
    DEPENDS ${mxvk_SPRITE3D_VERTEX_SHADER}
    COMMENT "Compiling MXVK 3D sprite vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${mxvk_SPRITE3D_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${mxvk_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${mxvk_SPRITE3D_FRAGMENT_SHADER} -o ${mxvk_SPRITE3D_FRAGMENT_SPV}
    DEPENDS ${mxvk_SPRITE3D_FRAGMENT_SHADER}
    COMMENT "Compiling MXVK 3D sprite fragment shader"
    VERBATIM
)

add_custom_target(mxvk_shaders
    DEPENDS
        ${mxvk_VERTEX_SPV}
        ${mxvk_FRAGMENT_SPV}
    ${mxvk_TEXT_VERTEX_SPV}
    ${mxvk_TEXT_FRAGMENT_SPV}
    ${mxvk_SPRITE3D_VERTEX_SPV}
    ${mxvk_SPRITE3D_FRAGMENT_SPV}
)
if(CV)
    target_sources(mxvk PRIVATE mxvk/src/mxvk_cv.cpp)
endif()
add_library(mxvk::mxvk ALIAS mxvk)
target_include_directories(mxvk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/mxvk/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/mxvk/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

function(mxvk_configure_mingw_target target_name)
    if(NOT MINGW OR NOT TARGET ${target_name})
        return()
    endif()

    set(is_gnu_like_mingw FALSE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(is_gnu_like_mingw TRUE)
        target_compile_options(${target_name} PRIVATE -Wa,-mbig-obj)
    endif()

    get_target_property(target_type ${target_name} TYPE)
    if(target_type STREQUAL "EXECUTABLE")
        set_target_properties(${target_name} PROPERTIES WIN32_EXECUTABLE TRUE)

        if(is_gnu_like_mingw)
            target_link_options(${target_name} PRIVATE -mwindows)
        endif()
    endif()
endfunction()

mxvk_configure_mingw_target(mxvk)

if(DEFINED GLM_INCLUDE_DIRS AND NOT TARGET glm::glm)
    if(MINGW AND CMAKE_CROSSCOMPILING)
        if(DEFINED MXVK_MINGW_PREFIX)
            foreach(glm_include_dir IN LISTS GLM_INCLUDE_DIRS)
                if(glm_include_dir MATCHES "^${MXVK_MINGW_PREFIX}(/|$)")
                    target_include_directories(mxvk PUBLIC ${glm_include_dir})
                endif()
            endforeach()
        endif()
    else()
        target_include_directories(mxvk PUBLIC ${GLM_INCLUDE_DIRS})
    endif()
endif()
if(CV OR MXVK_CUDA_ENABLED)
    target_include_directories(mxvk PUBLIC ${OpenCV_INCLUDE_DIRS})
endif()
if(CV)
    target_compile_definitions(mxvk PUBLIC MXVK_WITH_CV)
endif()
if(MXVK_CUDA_ENABLED)
    if(MXVK_CUDA_USE_CUDAToolkit)
        target_link_libraries(mxvk PUBLIC CUDA::cudart)
    else()
        target_include_directories(mxvk PUBLIC ${CUDA_INCLUDE_DIRS})
        target_link_libraries(mxvk PUBLIC ${CUDA_LIBRARIES})
    endif()
    target_compile_definitions(mxvk PUBLIC MXVK_CUDA)
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
    if(NOT (MINGW AND CMAKE_CROSSCOMPILING))
        target_link_libraries(mxvk PUBLIC glm::glm)
    endif()
endif()
add_dependencies(mxvk mxvk_shaders)
target_compile_definitions(mxvk PUBLIC
    MXVK_SPRITE_SHADER_DIR="${mxvk_SHADER_OUTPUT_DIR}"
    MXVK_TEXT_SHADER_DIR="${mxvk_SHADER_OUTPUT_DIR}"
    MXVK_SPRITE3D_SHADER_DIR="${mxvk_SHADER_OUTPUT_DIR}"
)
if(CV OR MXVK_CUDA_ENABLED)
    target_link_libraries(mxvk PUBLIC ${OpenCV_LIBS})
endif()
if(EXAMPLES)
    message(STATUS "Examples ON")
    add_subdirectory(examples/hello_world)
    mxvk_configure_mingw_target(hello_world)
    add_subdirectory(examples/static_example)
    mxvk_configure_mingw_target(static_example)
    add_subdirectory(examples/sprite_example)
    mxvk_configure_mingw_target(sprite_example)
    add_subdirectory(examples/sprite3d_example)
    mxvk_configure_mingw_target(sprite3d_example)
    add_subdirectory(examples/text_example)
    mxvk_configure_mingw_target(text_example)
    add_subdirectory(examples/model_example)
    mxvk_configure_mingw_target(model_example)
    add_subdirectory(examples/starship)
    mxvk_configure_mingw_target(starship)
    add_subdirectory(examples/planet)
    mxvk_configure_mingw_target(planet)
    add_subdirectory(examples/tux_example)
    mxvk_configure_mingw_target(tux_example)
    add_subdirectory(examples/glitch_cube)
    mxvk_configure_mingw_target(glitch_cube)
    add_subdirectory(examples/asteroids)
    mxvk_configure_mingw_target(asteroids)
    add_subdirectory(examples/pong)
    mxvk_configure_mingw_target(pong)
    add_subdirectory(examples/tetris)
    mxvk_configure_mingw_target(tetris)
    add_subdirectory(examples/tictactoe)
    mxvk_configure_mingw_target(tictactoe)
    add_subdirectory(examples/puzzle)
    mxvk_configure_mingw_target(puzzle)
    add_subdirectory(examples/pool_demo)
    mxvk_configure_mingw_target(Pool3D)
    add_subdirectory(examples/fractal_zoom)
    mxvk_configure_mingw_target(fractal_zoom)
    add_subdirectory(examples/masterpiece)
    mxvk_configure_mingw_target(masterpiece)
    add_subdirectory(examples/console_demo)
    mxvk_configure_mingw_target(console_demo)
    add_subdirectory(examples/matrix)
    mxvk_configure_mingw_target(matrix)
    add_subdirectory(examples/binary_matrix)
    mxvk_configure_mingw_target(binary_matrix)
    add_subdirectory(examples/walk)
    mxvk_configure_mingw_target(walk_example)
    if(CV)
        add_subdirectory(examples/compute_shader)
        mxvk_configure_mingw_target(compute_shader)
        add_subdirectory(examples/opencv_example)
        mxvk_configure_mingw_target(opencv_example)
        add_subdirectory(examples/opencv_model)
        mxvk_configure_mingw_target(opencv_model)
    endif()

    function(mxvk_copy_runtime_shaders target_name)
        if(NOT TARGET ${target_name})
            return()
        endif()

        get_target_property(target_runtime_dir ${target_name} RUNTIME_OUTPUT_DIRECTORY)
        if(NOT target_runtime_dir)
            get_target_property(target_runtime_dir ${target_name} BINARY_DIR)
        endif()

        string(MAKE_C_IDENTIFIER "${target_name}" target_name_id)
        set(copy_target "mxvk_copy_runtime_shaders_${target_name_id}")

        set(copy_commands
            COMMAND ${CMAKE_COMMAND} -E make_directory ${target_runtime_dir}/data
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${mxvk_VERTEX_SPV}
                ${target_runtime_dir}/data/sprite.vert.spv
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${mxvk_VERTEX_SPV}
                ${target_runtime_dir}/data/sprite_vert.spv
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${mxvk_FRAGMENT_SPV}
                ${target_runtime_dir}/data/sprite.frag.spv
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${mxvk_FRAGMENT_SPV}
                ${target_runtime_dir}/data/sprite_frag.spv
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${mxvk_TEXT_VERTEX_SPV}
                ${target_runtime_dir}/data/text.vert.spv
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${mxvk_TEXT_VERTEX_SPV}
                ${target_runtime_dir}/data/text_vert.spv
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${mxvk_TEXT_FRAGMENT_SPV}
                ${target_runtime_dir}/data/text.frag.spv
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${mxvk_TEXT_FRAGMENT_SPV}
                ${target_runtime_dir}/data/text_frag.spv
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${mxvk_SPRITE3D_VERTEX_SPV}
                ${target_runtime_dir}/data/sprite3d.vert.spv
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${mxvk_SPRITE3D_FRAGMENT_SPV}
                ${target_runtime_dir}/data/sprite3d.frag.spv
        )

        get_target_property(target_source_dir ${target_name} SOURCE_DIR)
        if(target_source_dir AND EXISTS "${target_source_dir}/data")
            list(APPEND copy_commands
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${mxvk_VERTEX_SPV}
                    ${target_source_dir}/data/sprite.vert.spv
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${mxvk_VERTEX_SPV}
                    ${target_source_dir}/data/sprite_vert.spv
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${mxvk_FRAGMENT_SPV}
                    ${target_source_dir}/data/sprite.frag.spv
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${mxvk_FRAGMENT_SPV}
                    ${target_source_dir}/data/sprite_frag.spv
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${mxvk_TEXT_VERTEX_SPV}
                    ${target_source_dir}/data/text.vert.spv
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${mxvk_TEXT_VERTEX_SPV}
                    ${target_source_dir}/data/text_vert.spv
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${mxvk_TEXT_FRAGMENT_SPV}
                    ${target_source_dir}/data/text.frag.spv
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${mxvk_TEXT_FRAGMENT_SPV}
                    ${target_source_dir}/data/text_frag.spv
            )
        endif()

        add_custom_target(${copy_target}
            ${copy_commands}
            COMMENT "Copying MXVK sprite/text runtime shaders to ${target_name} data directory"
            VERBATIM
        )
        add_dependencies(${copy_target} mxvk_shaders)
        add_dependencies(${target_name} ${copy_target})
    endfunction()

    set(mxvk_example_targets
        hello_world
        static_example
        sprite_example
        sprite3d_example
        text_example
        model_example
        planet
        tux_example
        glitch_cube
        asteroids
        pong
        tictactoe
        puzzle
        Pool3D
        fractal_zoom
        console_demo
        matrix
        binary_matrix
        walk_example
    )
    if(CV)
        list(APPEND mxvk_example_targets compute_shader opencv_example opencv_model)
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

install(FILES
    ${mxvk_VERSION_HEADER_OUTPUT}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/mxvk
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
