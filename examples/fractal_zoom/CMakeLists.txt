add_executable(fractal_zoom
    ${CMAKE_SOURCE_DIR}/examples/fractal_zoom/fractal.cpp
)

target_link_libraries(fractal_zoom PRIVATE mxvk::mxvk)

set(fractal_zoom_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(fractal_zoom_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

set(fractal_zoom_VERTEX_SHADER ${fractal_zoom_SHADER_SOURCE_DIR}/fractal.vert)
set(fractal_zoom_FRAGMENT_SHADER ${fractal_zoom_SHADER_SOURCE_DIR}/fractal.frag)
set(fractal_zoom_VERTEX_SPV ${fractal_zoom_SHADER_OUTPUT_DIR}/fractal.vert.spv)
set(fractal_zoom_FRAGMENT_SPV ${fractal_zoom_SHADER_OUTPUT_DIR}/fractal.frag.spv)

add_custom_command(
    OUTPUT ${fractal_zoom_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${fractal_zoom_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${fractal_zoom_VERTEX_SHADER} -o ${fractal_zoom_VERTEX_SPV}
    DEPENDS ${fractal_zoom_VERTEX_SHADER}
    COMMENT "Compiling fractal_zoom vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${fractal_zoom_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${fractal_zoom_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${fractal_zoom_FRAGMENT_SHADER} -o ${fractal_zoom_FRAGMENT_SPV}
    DEPENDS ${fractal_zoom_FRAGMENT_SHADER}
    COMMENT "Compiling fractal_zoom fragment shader"
    VERBATIM
)

add_custom_target(fractal_zoom_shaders
    DEPENDS
        ${fractal_zoom_VERTEX_SPV}
        ${fractal_zoom_FRAGMENT_SPV}
)

add_dependencies(fractal_zoom fractal_zoom_shaders)

target_compile_definitions(fractal_zoom PRIVATE
    fractal_zoom_SHADER_DIR="${fractal_zoom_SHADER_OUTPUT_DIR}"
)
