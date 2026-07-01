add_executable(tictactoe
    ${CMAKE_SOURCE_DIR}/examples/tictactoe/main.cpp
)
target_link_libraries(tictactoe PRIVATE mxvk::mxvk)

target_compile_definitions(tictactoe PRIVATE
    tictactoe_ASSET_DIR="$<TARGET_FILE_DIR:tictactoe>"
    tictactoe_FONT_PATH="$<TARGET_FILE_DIR:tictactoe>/data/font.ttf"
)

add_custom_command(TARGET tictactoe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:tictactoe>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/text_example/data/font.ttf
        $<TARGET_FILE_DIR:tictactoe>/data/font.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/tictactoe/data/bg.png
        $<TARGET_FILE_DIR:tictactoe>/data/bg.png
    COMMENT "Copying tic-tac-toe runtime data"
)
