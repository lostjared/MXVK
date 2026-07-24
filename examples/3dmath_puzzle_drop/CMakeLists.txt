add_executable(math3d_puzzle_drop
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

target_link_libraries(math3d_puzzle_drop PRIVATE mxvk::mxvk)
mxvk_configure_3dmath_backend(math3d_puzzle_drop)

set_target_properties(math3d_puzzle_drop PROPERTIES
    OUTPUT_NAME "3dmath_puzzle_drop"
)

target_compile_definitions(math3d_puzzle_drop PRIVATE
    math3d_puzzle_drop_ASSET_DIR="$<TARGET_FILE_DIR:math3d_puzzle_drop>"
)

add_custom_command(TARGET math3d_puzzle_drop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:math3d_puzzle_drop>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/puzzle_drop/data/red1.png
        ${CMAKE_SOURCE_DIR}/examples/puzzle_drop/data/red2.png
        ${CMAKE_SOURCE_DIR}/examples/puzzle_drop/data/red3.png
        ${CMAKE_SOURCE_DIR}/examples/puzzle_drop/data/green1.png
        ${CMAKE_SOURCE_DIR}/examples/puzzle_drop/data/green2.png
        ${CMAKE_SOURCE_DIR}/examples/puzzle_drop/data/green3.png
        ${CMAKE_SOURCE_DIR}/examples/puzzle_drop/data/blue1.png
        ${CMAKE_SOURCE_DIR}/examples/puzzle_drop/data/blue2.png
        ${CMAKE_SOURCE_DIR}/examples/puzzle_drop/data/blue3.png
        ${CMAKE_SOURCE_DIR}/examples/puzzle_drop/data/level1.png
        ${CMAKE_SOURCE_DIR}/examples/puzzle_drop/data/intro1.png
        ${CMAKE_SOURCE_DIR}/examples/tictactoe/data/font.ttf
        $<TARGET_FILE_DIR:math3d_puzzle_drop>/data
    COMMENT "Copying 3dmath_puzzle_drop assets to runtime output directory"
)
