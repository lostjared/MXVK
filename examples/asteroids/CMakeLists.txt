add_executable(vk_spacerox
    ${CMAKE_SOURCE_DIR}/examples/asteroids/space.cpp
)
target_link_libraries(vk_spacerox PRIVATE mxvk::mxvk)

target_compile_definitions(vk_spacerox PRIVATE
    asteroids_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_custom_command(TARGET vk_spacerox POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:vk_spacerox>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:vk_spacerox>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/font.ttf
        $<TARGET_FILE_DIR:vk_spacerox>/font.ttf
    COMMENT "Copying asteroids assets to runtime output directory"
)
