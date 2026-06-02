add_executable(glitch_cube
    ${CMAKE_SOURCE_DIR}/examples/glitch_cube/glitch.cpp
)
target_link_libraries(glitch_cube PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(GLITCH_CUBE_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(GLITCH_CUBE_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

set(GLITCH_CUBE_MODEL_VERTEX_SHADER ${GLITCH_CUBE_SHADER_SOURCE_DIR}/model.vert)
set(GLITCH_CUBE_MODEL_FRAGMENT_SHADER ${GLITCH_CUBE_SHADER_SOURCE_DIR}/model.frag)
set(GLITCH_CUBE_BACKGROUND_VERTEX_SHADER ${GLITCH_CUBE_SHADER_SOURCE_DIR}/background.vert)
set(GLITCH_CUBE_BACKGROUND_FRAGMENT_SHADER ${GLITCH_CUBE_SHADER_SOURCE_DIR}/background.frag)

set(GLITCH_CUBE_MODEL_VERTEX_SPV ${GLITCH_CUBE_SHADER_OUTPUT_DIR}/model.vert.spv)
set(GLITCH_CUBE_MODEL_FRAGMENT_SPV ${GLITCH_CUBE_SHADER_OUTPUT_DIR}/model.frag.spv)
set(GLITCH_CUBE_BACKGROUND_VERTEX_SPV ${GLITCH_CUBE_SHADER_OUTPUT_DIR}/background.vert.spv)
set(GLITCH_CUBE_BACKGROUND_FRAGMENT_SPV ${GLITCH_CUBE_SHADER_OUTPUT_DIR}/background.frag.spv)

add_custom_command(
    OUTPUT ${GLITCH_CUBE_MODEL_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GLITCH_CUBE_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${GLITCH_CUBE_MODEL_VERTEX_SHADER} -o ${GLITCH_CUBE_MODEL_VERTEX_SPV}
    DEPENDS ${GLITCH_CUBE_MODEL_VERTEX_SHADER}
    COMMENT "Compiling glitch_cube model vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${GLITCH_CUBE_MODEL_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GLITCH_CUBE_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${GLITCH_CUBE_MODEL_FRAGMENT_SHADER} -o ${GLITCH_CUBE_MODEL_FRAGMENT_SPV}
    DEPENDS ${GLITCH_CUBE_MODEL_FRAGMENT_SHADER}
    COMMENT "Compiling glitch_cube model fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${GLITCH_CUBE_BACKGROUND_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GLITCH_CUBE_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${GLITCH_CUBE_BACKGROUND_VERTEX_SHADER} -o ${GLITCH_CUBE_BACKGROUND_VERTEX_SPV}
    DEPENDS ${GLITCH_CUBE_BACKGROUND_VERTEX_SHADER}
    COMMENT "Compiling glitch_cube background vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${GLITCH_CUBE_BACKGROUND_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${GLITCH_CUBE_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${GLITCH_CUBE_BACKGROUND_FRAGMENT_SHADER} -o ${GLITCH_CUBE_BACKGROUND_FRAGMENT_SPV}
    DEPENDS ${GLITCH_CUBE_BACKGROUND_FRAGMENT_SHADER}
    COMMENT "Compiling glitch_cube background fragment shader"
    VERBATIM
)

add_custom_target(glitch_cube_shaders
    DEPENDS
        ${GLITCH_CUBE_MODEL_VERTEX_SPV}
        ${GLITCH_CUBE_MODEL_FRAGMENT_SPV}
        ${GLITCH_CUBE_BACKGROUND_VERTEX_SPV}
        ${GLITCH_CUBE_BACKGROUND_FRAGMENT_SPV}
)

add_dependencies(glitch_cube glitch_cube_shaders)
target_compile_definitions(glitch_cube PRIVATE
    GLITCH_CUBE_SHADER_DIR="${GLITCH_CUBE_SHADER_OUTPUT_DIR}"
    GLITCH_CUBE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_custom_command(TARGET glitch_cube POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:glitch_cube>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:glitch_cube>/data
    COMMENT "Copying glitch_cube assets to runtime output directory"
)
