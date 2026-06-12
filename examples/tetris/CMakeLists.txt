add_executable(tetris
    ${CMAKE_SOURCE_DIR}/examples/tetris/tetris.cpp
)

target_link_libraries(tetris PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(TETRIS_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(TETRIS_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(TETRIS_VERTEX_SHADER ${TETRIS_SHADER_SOURCE_DIR}/model.vert)
set(TETRIS_FRAGMENT_SHADER ${TETRIS_SHADER_SOURCE_DIR}/model.frag)
set(TETRIS_VERTEX_SPV ${TETRIS_SHADER_OUTPUT_DIR}/tetris_model.vert.spv)
set(TETRIS_FRAGMENT_SPV ${TETRIS_SHADER_OUTPUT_DIR}/tetris_model.frag.spv)

add_custom_command(
    OUTPUT ${TETRIS_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${TETRIS_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${TETRIS_VERTEX_SHADER} -o ${TETRIS_VERTEX_SPV}
    DEPENDS ${TETRIS_VERTEX_SHADER}
    COMMENT "Compiling tetris vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${TETRIS_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${TETRIS_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${TETRIS_FRAGMENT_SHADER} -o ${TETRIS_FRAGMENT_SPV}
    DEPENDS ${TETRIS_FRAGMENT_SHADER}
    COMMENT "Compiling tetris fragment shader"
    VERBATIM
)

add_custom_target(tetris_shaders
    DEPENDS
        ${TETRIS_VERTEX_SPV}
        ${TETRIS_FRAGMENT_SPV}
)

add_dependencies(tetris tetris_shaders)

target_compile_definitions(tetris PRIVATE
    tetris_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    tetris_FONT_PATH="${CMAKE_SOURCE_DIR}/examples/tictactoe/data/font.ttf"
    tetris_SHADER_DIR="${TETRIS_SHADER_OUTPUT_DIR}"
)

add_custom_command(TARGET tetris POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:tetris>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:tetris>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/tictactoe/data/font.ttf
        $<TARGET_FILE_DIR:tetris>/data/font.ttf
    COMMENT "Copying tetris assets to runtime output directory"
)
