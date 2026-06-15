add_executable(tux_example
    ${CMAKE_SOURCE_DIR}/examples/tux_example/main.cpp
)
target_link_libraries(tux_example PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(tux_example_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(tux_example_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

set(tux_example_VERTEX_SHADER ${tux_example_SHADER_SOURCE_DIR}/model.vert)
set(tux_example_FRAGMENT_SHADER ${tux_example_SHADER_SOURCE_DIR}/model.frag)
set(tux_example_VERTEX_SPV ${tux_example_SHADER_OUTPUT_DIR}/model.vert.spv)
set(tux_example_FRAGMENT_SPV ${tux_example_SHADER_OUTPUT_DIR}/model.frag.spv)
set(tux_example_BACKGROUND_VERTEX_SHADER ${tux_example_SHADER_SOURCE_DIR}/background.vert)
set(tux_example_BACKGROUND_FRAGMENT_SHADER ${tux_example_SHADER_SOURCE_DIR}/background.frag)
set(tux_example_BACKGROUND_VERTEX_SPV ${tux_example_SHADER_OUTPUT_DIR}/background.vert.spv)
set(tux_example_BACKGROUND_FRAGMENT_SPV ${tux_example_SHADER_OUTPUT_DIR}/background.frag.spv)
set(tux_example_SNOWFLAKE_FRAGMENT_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/data/snowflake.frag)
set(tux_example_SNOWFLAKE_FRAGMENT_SPV ${tux_example_SHADER_OUTPUT_DIR}/snowflake.frag.spv)

add_custom_command(
    OUTPUT ${tux_example_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${tux_example_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${tux_example_VERTEX_SHADER} -o ${tux_example_VERTEX_SPV}
    DEPENDS ${tux_example_VERTEX_SHADER}
    COMMENT "Compiling model example vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${tux_example_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${tux_example_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${tux_example_FRAGMENT_SHADER} -o ${tux_example_FRAGMENT_SPV}
    DEPENDS ${tux_example_FRAGMENT_SHADER}
    COMMENT "Compiling model example fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${tux_example_BACKGROUND_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${tux_example_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${tux_example_BACKGROUND_VERTEX_SHADER} -o ${tux_example_BACKGROUND_VERTEX_SPV}
    DEPENDS ${tux_example_BACKGROUND_VERTEX_SHADER}
    COMMENT "Compiling tux example background vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${tux_example_BACKGROUND_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${tux_example_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${tux_example_BACKGROUND_FRAGMENT_SHADER} -o ${tux_example_BACKGROUND_FRAGMENT_SPV}
    DEPENDS ${tux_example_BACKGROUND_FRAGMENT_SHADER}
    COMMENT "Compiling tux example background fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${tux_example_SNOWFLAKE_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${tux_example_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${tux_example_SNOWFLAKE_FRAGMENT_SHADER} -o ${tux_example_SNOWFLAKE_FRAGMENT_SPV}
    DEPENDS ${tux_example_SNOWFLAKE_FRAGMENT_SHADER}
    COMMENT "Compiling tux example snowflake fragment shader"
    VERBATIM
)

add_custom_target(tux_example_shaders
    DEPENDS
        ${tux_example_VERTEX_SPV}
        ${tux_example_FRAGMENT_SPV}
    ${tux_example_BACKGROUND_VERTEX_SPV}
    ${tux_example_BACKGROUND_FRAGMENT_SPV}
    ${tux_example_SNOWFLAKE_FRAGMENT_SPV}
)

add_dependencies(tux_example tux_example_shaders)
target_compile_definitions(tux_example PRIVATE
    tux_example_SHADER_DIR="${tux_example_SHADER_OUTPUT_DIR}"
    tux_example_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

set(tux_example_FONT_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/data/font.ttf)

add_custom_command(TARGET tux_example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:tux_example>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:tux_example>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${tux_example_FONT_SOURCE}
        $<TARGET_FILE_DIR:tux_example>/data/font.ttf
    COMMENT "Copying tux example assets to runtime output directory"
)
