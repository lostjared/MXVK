add_executable(static_example
    ${CMAKE_SOURCE_DIR}/examples/static_example/main.cpp
)
target_link_libraries(static_example PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(static_example_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(static_example_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(static_example_RUNTIME_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)

set(static_example_VERTEX_SHADER ${static_example_SHADER_SOURCE_DIR}/triangle.vert)
set(static_example_FRAGMENT_SHADER ${static_example_SHADER_SOURCE_DIR}/triangle.frag)
set(static_example_VERTEX_SPV ${static_example_SHADER_OUTPUT_DIR}/triangle.vert.spv)
set(static_example_FRAGMENT_SPV ${static_example_SHADER_OUTPUT_DIR}/triangle.frag.spv)

add_custom_command(
    OUTPUT ${static_example_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${static_example_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${static_example_VERTEX_SHADER} -o ${static_example_VERTEX_SPV}
    DEPENDS ${static_example_VERTEX_SHADER}
    COMMENT "Compiling triangle vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${static_example_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${static_example_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${static_example_FRAGMENT_SHADER} -o ${static_example_FRAGMENT_SPV}
    DEPENDS ${static_example_FRAGMENT_SHADER}
    COMMENT "Compiling triangle fragment shader"
    VERBATIM
)

add_custom_target(static_example_shaders
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${static_example_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${static_example_VERTEX_SPV}
        ${static_example_RUNTIME_DATA_DIR}/triangle.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${static_example_FRAGMENT_SPV}
        ${static_example_RUNTIME_DATA_DIR}/triangle.frag.spv
    DEPENDS
        ${static_example_VERTEX_SPV}
        ${static_example_FRAGMENT_SPV}
    COMMENT "Copying static_example SPIR-V shaders to runtime data directory"
    VERBATIM
)

add_dependencies(static_example static_example_shaders)
target_compile_definitions(static_example PRIVATE
    static_example_ASSET_DIR="$<TARGET_FILE_DIR:static_example>"
)
