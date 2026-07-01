add_executable(puzzle
    ${CMAKE_SOURCE_DIR}/examples/puzzle/acid.drop.cpp
)

target_link_libraries(puzzle PRIVATE mxvk::mxvk)

target_compile_definitions(puzzle PRIVATE
    puzzle_ASSET_DIR="$<TARGET_FILE_DIR:puzzle>"
)

add_custom_command(TARGET puzzle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:puzzle>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:puzzle>/data
    COMMENT "Copying puzzle assets to runtime output directory"
)
