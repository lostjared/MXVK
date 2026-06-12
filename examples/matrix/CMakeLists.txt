add_executable(matrix
    ${CMAKE_SOURCE_DIR}/examples/matrix/matrix.cpp
)

target_link_libraries(matrix PRIVATE mxvk::mxvk)

target_compile_definitions(matrix PRIVATE
    matrix_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_custom_command(TARGET matrix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:matrix>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/keifont.ttf
        $<TARGET_FILE_DIR:matrix>/data/keifont.ttf
    COMMENT "Copying matrix assets to runtime output directory"
)
