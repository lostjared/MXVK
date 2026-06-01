add_executable(text_example
    ${CMAKE_SOURCE_DIR}/examples/text_example/main.cpp
)
target_link_libraries(text_example PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(text_example_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(text_example_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

set(text_example_VERTEX_SHADER ${text_example_SHADER_SOURCE_DIR}/vertex.vert)
set(text_example_FRAGMENT_SHADER ${text_example_SHADER_SOURCE_DIR}/fragment.frag)
set(text_example_VERTEX_SPV ${text_example_SHADER_OUTPUT_DIR}/vertex.vert.spv)
set(text_example_FRAGMENT_SPV ${text_example_SHADER_OUTPUT_DIR}/fragment.frag.spv)

add_custom_command(
    OUTPUT ${text_example_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${text_example_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${text_example_VERTEX_SHADER} -o ${text_example_VERTEX_SPV}
    DEPENDS ${text_example_VERTEX_SHADER}
    COMMENT "Compiling triangle vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${text_example_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${text_example_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${text_example_FRAGMENT_SHADER} -o ${text_example_FRAGMENT_SPV}
    DEPENDS ${text_example_FRAGMENT_SHADER}
    COMMENT "Compiling triangle fragment shader"
    VERBATIM
)

add_custom_target(text_example_shaders
    DEPENDS
        ${text_example_VERTEX_SPV}
        ${text_example_FRAGMENT_SPV}
)

add_dependencies(text_example text_example_shaders)
target_compile_definitions(text_example PRIVATE
    text_example_SHADER_DIR="${text_example_SHADER_OUTPUT_DIR}"
    text_example_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

set(text_example_FONT_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/data/font.ttf)

add_custom_command(TARGET text_example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:text_example>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${text_example_FONT_SOURCE}
        $<TARGET_FILE_DIR:text_example>/data/font.ttf
    COMMENT "Copying text example font.ttf to runtime output directory"
)
