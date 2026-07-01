add_executable(hello_world
    ${CMAKE_SOURCE_DIR}/examples/hello_world/main.cpp
)
target_link_libraries(hello_world PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(HELLO_WORLD_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(HELLO_WORLD_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(HELLO_WORLD_RUNTIME_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)

set(HELLO_WORLD_VERTEX_SHADER ${HELLO_WORLD_SHADER_SOURCE_DIR}/triangle.vert)
set(HELLO_WORLD_FRAGMENT_SHADER ${HELLO_WORLD_SHADER_SOURCE_DIR}/triangle.frag)
set(HELLO_WORLD_VERTEX_SPV ${HELLO_WORLD_SHADER_OUTPUT_DIR}/triangle.vert.spv)
set(HELLO_WORLD_FRAGMENT_SPV ${HELLO_WORLD_SHADER_OUTPUT_DIR}/triangle.frag.spv)

add_custom_command(
    OUTPUT ${HELLO_WORLD_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${HELLO_WORLD_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${HELLO_WORLD_VERTEX_SHADER} -o ${HELLO_WORLD_VERTEX_SPV}
    DEPENDS ${HELLO_WORLD_VERTEX_SHADER}
    COMMENT "Compiling triangle vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${HELLO_WORLD_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${HELLO_WORLD_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${HELLO_WORLD_FRAGMENT_SHADER} -o ${HELLO_WORLD_FRAGMENT_SPV}
    DEPENDS ${HELLO_WORLD_FRAGMENT_SHADER}
    COMMENT "Compiling triangle fragment shader"
    VERBATIM
)

add_custom_target(hello_world_shaders
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${HELLO_WORLD_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${HELLO_WORLD_VERTEX_SPV}
        ${HELLO_WORLD_RUNTIME_DATA_DIR}/triangle.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${HELLO_WORLD_FRAGMENT_SPV}
        ${HELLO_WORLD_RUNTIME_DATA_DIR}/triangle.frag.spv
    DEPENDS
        ${HELLO_WORLD_VERTEX_SPV}
        ${HELLO_WORLD_FRAGMENT_SPV}
    COMMENT "Copying hello_world SPIR-V shaders to runtime data directory"
    VERBATIM
)

add_dependencies(hello_world hello_world_shaders)
target_compile_definitions(hello_world PRIVATE
    HELLO_WORLD_ASSET_DIR="$<TARGET_FILE_DIR:hello_world>"
)
