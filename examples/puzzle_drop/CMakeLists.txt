add_executable(puzzle_drop
    ${CMAKE_SOURCE_DIR}/examples/puzzle_drop/puzzle_drop.cpp
)

target_link_libraries(puzzle_drop PRIVATE rain)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(PUZZLE_DROP_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(PUZZLE_DROP_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(PUZZLE_DROP_VERTEX_SHADER ${PUZZLE_DROP_SHADER_SOURCE_DIR}/puzzle_drop_piece.vert)
set(PUZZLE_DROP_FRAGMENT_SHADER ${PUZZLE_DROP_SHADER_SOURCE_DIR}/puzzle_drop_piece.frag)
set(PUZZLE_DROP_BACKGROUND_FRAGMENT_SHADER ${PUZZLE_DROP_SHADER_SOURCE_DIR}/puzzle_drop_background.frag)
set(PUZZLE_DROP_INTRO_FRAGMENT_SHADER ${PUZZLE_DROP_SHADER_SOURCE_DIR}/intro.frag)
set(PUZZLE_DROP_VERTEX_SPV ${PUZZLE_DROP_SHADER_OUTPUT_DIR}/puzzle_drop_piece.vert.spv)
set(PUZZLE_DROP_FRAGMENT_SPV ${PUZZLE_DROP_SHADER_OUTPUT_DIR}/puzzle_drop_piece.frag.spv)
set(PUZZLE_DROP_BACKGROUND_FRAGMENT_SPV ${PUZZLE_DROP_SHADER_OUTPUT_DIR}/puzzle_drop_background.frag.spv)
set(PUZZLE_DROP_INTRO_FRAGMENT_SPV ${PUZZLE_DROP_SHADER_OUTPUT_DIR}/intro.frag.spv)

add_custom_command(
    OUTPUT ${PUZZLE_DROP_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PUZZLE_DROP_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${PUZZLE_DROP_VERTEX_SHADER} -o ${PUZZLE_DROP_VERTEX_SPV}
    DEPENDS ${PUZZLE_DROP_VERTEX_SHADER}
    COMMENT "Compiling puzzle_drop vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${PUZZLE_DROP_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PUZZLE_DROP_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${PUZZLE_DROP_FRAGMENT_SHADER} -o ${PUZZLE_DROP_FRAGMENT_SPV}
    DEPENDS ${PUZZLE_DROP_FRAGMENT_SHADER}
    COMMENT "Compiling puzzle_drop fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${PUZZLE_DROP_BACKGROUND_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PUZZLE_DROP_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${PUZZLE_DROP_BACKGROUND_FRAGMENT_SHADER} -o ${PUZZLE_DROP_BACKGROUND_FRAGMENT_SPV}
    DEPENDS ${PUZZLE_DROP_BACKGROUND_FRAGMENT_SHADER}
    COMMENT "Compiling puzzle_drop background fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${PUZZLE_DROP_INTRO_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PUZZLE_DROP_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${PUZZLE_DROP_INTRO_FRAGMENT_SHADER} -o ${PUZZLE_DROP_INTRO_FRAGMENT_SPV}
    DEPENDS ${PUZZLE_DROP_INTRO_FRAGMENT_SHADER}
    COMMENT "Compiling puzzle_drop intro fragment shader"
    VERBATIM
)

add_custom_target(puzzle_drop_shaders
    DEPENDS
        ${PUZZLE_DROP_VERTEX_SPV}
        ${PUZZLE_DROP_FRAGMENT_SPV}
        ${PUZZLE_DROP_BACKGROUND_FRAGMENT_SPV}
        ${PUZZLE_DROP_INTRO_FRAGMENT_SPV}
)

add_dependencies(puzzle_drop puzzle_drop_shaders)

target_compile_definitions(puzzle_drop PRIVATE
    puzzle_drop_ASSET_DIR="$<TARGET_FILE_DIR:puzzle_drop>"
    puzzle_drop_TETRIS_DATA_DIR="$<TARGET_FILE_DIR:puzzle_drop>/data"
    puzzle_drop_FONT_PATH="$<TARGET_FILE_DIR:puzzle_drop>/data/font.ttf"
    puzzle_drop_SHADER_DIR="${PUZZLE_DROP_SHADER_OUTPUT_DIR}"
)

add_custom_command(TARGET puzzle_drop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:puzzle_drop>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:puzzle_drop>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/rain/data/NotoSansCJK-Bold.ttc
        $<TARGET_FILE_DIR:puzzle_drop>/data/NotoSansCJK-Bold.ttc
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/rain/data/NotoSansCJK-Regular.ttc
        $<TARGET_FILE_DIR:puzzle_drop>/data/NotoSansCJK-Regular.ttc
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/tictactoe/data/font.ttf
        $<TARGET_FILE_DIR:puzzle_drop>/data/font.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/tetris/data/cube.mxmod.z
        ${CMAKE_SOURCE_DIR}/examples/tetris/data/manifest_gray.txt
        ${CMAKE_SOURCE_DIR}/examples/tetris/data/block_gray.png
        $<TARGET_FILE_DIR:puzzle_drop>/data
    COMMENT "Copying puzzle_drop assets to runtime output directory"
)
