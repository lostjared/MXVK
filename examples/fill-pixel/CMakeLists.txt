if(NOT TARGET mxwrite)
    message(STATUS "Skipping fill_pixel: FFmpeg capture support is unavailable")
    return()
endif()

add_executable(fill_pixel main.cpp)
target_link_libraries(fill_pixel PRIVATE mxvk::mxvk mxwrite)

set(fill_pixel_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fill_pixel.frag)
set(fill_pixel_SPV ${CMAKE_CURRENT_BINARY_DIR}/fill_pixel.frag.spv)

add_custom_command(
    OUTPUT ${fill_pixel_SPV}
    COMMAND ${GLSLC_EXECUTABLE} ${fill_pixel_SHADER} -o ${fill_pixel_SPV}
    DEPENDS ${fill_pixel_SHADER}
    COMMENT "Compiling fill_pixel fragment shader"
    VERBATIM
)
add_custom_target(fill_pixel_shader DEPENDS ${fill_pixel_SPV})
add_dependencies(fill_pixel fill_pixel_shader)

add_custom_command(TARGET fill_pixel POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:fill_pixel>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${fill_pixel_SPV} $<TARGET_FILE_DIR:fill_pixel>/shaders/fill_pixel.frag.spv
    VERBATIM
)
