add_executable(asteroids
    ${CMAKE_SOURCE_DIR}/examples/asteroids/space.cpp
)
target_link_libraries(asteroids PRIVATE mxvk::mxvk)

target_compile_definitions(asteroids PRIVATE
    asteroids_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_custom_command(TARGET asteroids POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:asteroids>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:asteroids>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/font.ttf
        $<TARGET_FILE_DIR:asteroids>/font.ttf
    COMMENT "Copying asteroids assets to runtime output directory"
) 

