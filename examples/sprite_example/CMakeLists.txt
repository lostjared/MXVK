add_executable(sprite_example
    ${CMAKE_SOURCE_DIR}/examples/sprite_example/main.cpp
)
target_link_libraries(sprite_example PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(sprite_example_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(sprite_example_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

set(sprite_example_VERTEX_SHADER ${sprite_example_SHADER_SOURCE_DIR}/vertex.vert)
set(sprite_example_FRAGMENT_SHADER ${sprite_example_SHADER_SOURCE_DIR}/fragment.frag)
set(sprite_example_VERTEX_SPV ${sprite_example_SHADER_OUTPUT_DIR}/vertex.vert.spv)
set(sprite_example_FRAGMENT_SPV ${sprite_example_SHADER_OUTPUT_DIR}/fragment.frag.spv)

add_custom_command(
    OUTPUT ${sprite_example_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${sprite_example_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${sprite_example_VERTEX_SHADER} -o ${sprite_example_VERTEX_SPV}
    DEPENDS ${sprite_example_VERTEX_SHADER}
    COMMENT "Compiling triangle vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${sprite_example_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${sprite_example_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${sprite_example_FRAGMENT_SHADER} -o ${sprite_example_FRAGMENT_SPV}
    DEPENDS ${sprite_example_FRAGMENT_SHADER}
    COMMENT "Compiling triangle fragment shader"
    VERBATIM
)

add_custom_target(sprite_example_shaders
    DEPENDS
        ${sprite_example_VERTEX_SPV}
        ${sprite_example_FRAGMENT_SPV}
)

add_dependencies(sprite_example sprite_example_shaders)
target_compile_definitions(sprite_example PRIVATE
    sprite_example_SHADER_DIR="${sprite_example_SHADER_OUTPUT_DIR}"
    sprite_example_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

set(sprite_example_FONT_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/data/font.ttf)

add_custom_command(TARGET sprite_example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:sprite_example>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${sprite_example_FONT_SOURCE}
        $<TARGET_FILE_DIR:sprite_example>/data/font.ttf
    COMMENT "Copying sprite example font.ttf to runtime output directory"
)
