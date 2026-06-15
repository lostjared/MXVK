add_executable(sprite3d_example
    ${CMAKE_SOURCE_DIR}/examples/sprite3d_example/example.cpp
)
target_link_libraries(sprite3d_example PRIVATE mxvk::mxvk)

target_compile_definitions(sprite3d_example PRIVATE
    sprite3d_example_ASSET_DIR="$<TARGET_FILE_DIR:sprite3d_example>"
)

add_custom_command(TARGET sprite3d_example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:sprite3d_example>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:sprite3d_example>/data
    COMMENT "Copying sprite3d example assets to runtime output directory"
)
