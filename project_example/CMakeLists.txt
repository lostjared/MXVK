cmake_minimum_required(VERSION 3.20)
project(project_example LANGUAGES CXX)
find_package(SDL3_mixer REQUIRED)
find_package(mxvk CONFIG REQUIRED)
add_executable(project_example main.cpp)
target_compile_features(project_example PRIVATE cxx_std_20)
target_link_libraries(project_example PRIVATE mxvk::mxvk)
set(MXVK_RUNTIME_DATA_DIR "" CACHE PATH "Directory containing MXVK runtime SPIR-V shaders")

if(NOT MXVK_RUNTIME_DATA_DIR)
    get_filename_component(MXVK_CMAKE_DIR "${mxvk_DIR}" ABSOLUTE)
    get_filename_component(MXVK_PREFIX "${MXVK_CMAKE_DIR}/../../.." ABSOLUTE)
    foreach(candidate_dir
        "${MXVK_PREFIX}/share/mxvk/shaders"
        "${MXVK_PREFIX}/share/mxvk/data"
        "${MXVK_PREFIX}/shaders"
        "${MXVK_PREFIX}/data")
        if(EXISTS "${candidate_dir}/sprite.vert.spv")
            set(MXVK_RUNTIME_DATA_DIR "${candidate_dir}")
            break()
        endif()
    endforeach()
endif()

if(NOT MXVK_RUNTIME_DATA_DIR)
    message(FATAL_ERROR
        "Could not find MXVK runtime shaders. Set MXVK_RUNTIME_DATA_DIR to the directory containing sprite.vert.spv.")
endif()

set(MXVK_RUNTIME_SHADERS
    sprite.vert.spv
    sprite.frag.spv
    text.vert.spv
    text.frag.spv
    sprite3d.vert.spv
    sprite3d.frag.spv)

set(PROJECT_RUNTIME_DATA_DIR "${CMAKE_CURRENT_BINARY_DIR}/data")
add_custom_target(project_example_runtime_data ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_RUNTIME_DATA_DIR}"
    COMMENT "Copying MXVK runtime shaders"
    VERBATIM)

foreach(shader IN LISTS MXVK_RUNTIME_SHADERS)
    add_custom_command(TARGET project_example_runtime_data POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MXVK_RUNTIME_DATA_DIR}/${shader}"
            "${PROJECT_RUNTIME_DATA_DIR}/${shader}"
        VERBATIM)
endforeach()

add_dependencies(project_example project_example_runtime_data)
