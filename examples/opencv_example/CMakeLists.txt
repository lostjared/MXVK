add_executable(opencv_example
    ${CMAKE_SOURCE_DIR}/examples/opencv_example/main.cpp
)
target_link_libraries(opencv_example PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(opencv_example_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(opencv_example_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

set(opencv_example_VERTEX_SHADER ${opencv_example_SHADER_SOURCE_DIR}/vertex.vert)
set(opencv_example_FRAGMENT_SHADER ${opencv_example_SHADER_SOURCE_DIR}/fragment.frag)
set(opencv_example_VERTEX_SPV ${opencv_example_SHADER_OUTPUT_DIR}/vertex.vert.spv)
set(opencv_example_FRAGMENT_SPV ${opencv_example_SHADER_OUTPUT_DIR}/fragment.frag.spv)

add_custom_command(
    OUTPUT ${opencv_example_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${opencv_example_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${opencv_example_VERTEX_SHADER} -o ${opencv_example_VERTEX_SPV}
    DEPENDS ${opencv_example_VERTEX_SHADER}
    COMMENT "Compiling triangle vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${opencv_example_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${opencv_example_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${opencv_example_FRAGMENT_SHADER} -o ${opencv_example_FRAGMENT_SPV}
    DEPENDS ${opencv_example_FRAGMENT_SHADER}
    COMMENT "Compiling triangle fragment shader"
    VERBATIM
)

add_custom_target(opencv_example_shaders
    DEPENDS
        ${opencv_example_VERTEX_SPV}
        ${opencv_example_FRAGMENT_SPV}
)

add_dependencies(opencv_example opencv_example_shaders)
target_compile_definitions(opencv_example PRIVATE
    opencv_example_SHADER_DIR="${opencv_example_SHADER_OUTPUT_DIR}"
    opencv_example_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
