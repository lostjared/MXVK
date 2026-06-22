add_executable(dark
    ${CMAKE_SOURCE_DIR}/examples/dark/dark.cpp
)
target_link_libraries(dark PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(DARK_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(DARK_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

set(DARK_VERTEX_SHADER ${DARK_SHADER_SOURCE_DIR}/dark.vert)
set(DARK_FRAGMENT_SHADER ${DARK_SHADER_SOURCE_DIR}/dark.frag)
set(DARK_BEAM_VERTEX_SHADER ${DARK_SHADER_SOURCE_DIR}/beam3d.vert)
set(DARK_BEAM_FRAGMENT_SHADER ${DARK_SHADER_SOURCE_DIR}/beam3d.frag)
set(DARK_VERTEX_SPV ${DARK_SHADER_OUTPUT_DIR}/dark.vert.spv)
set(DARK_FRAGMENT_SPV ${DARK_SHADER_OUTPUT_DIR}/dark.frag.spv)
set(DARK_BEAM_VERTEX_SPV ${DARK_SHADER_OUTPUT_DIR}/beam3d.vert.spv)
set(DARK_BEAM_FRAGMENT_SPV ${DARK_SHADER_OUTPUT_DIR}/beam.frag.spv)

add_custom_command(
    OUTPUT ${DARK_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DARK_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${DARK_VERTEX_SHADER} -o ${DARK_VERTEX_SPV}
    DEPENDS ${DARK_VERTEX_SHADER}
    COMMENT "Compiling dark vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${DARK_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DARK_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${DARK_FRAGMENT_SHADER} -o ${DARK_FRAGMENT_SPV}
    DEPENDS ${DARK_FRAGMENT_SHADER}
    COMMENT "Compiling dark fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${DARK_BEAM_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DARK_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${DARK_BEAM_VERTEX_SHADER} -o ${DARK_BEAM_VERTEX_SPV}
    DEPENDS ${DARK_BEAM_VERTEX_SHADER}
    COMMENT "Compiling dark beam vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${DARK_BEAM_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DARK_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${DARK_BEAM_FRAGMENT_SHADER} -o ${DARK_BEAM_FRAGMENT_SPV}
    DEPENDS ${DARK_BEAM_FRAGMENT_SHADER}
    COMMENT "Compiling dark beam fragment shader"
    VERBATIM
)

add_custom_target(dark_shaders
    DEPENDS
        ${DARK_VERTEX_SPV}
        ${DARK_FRAGMENT_SPV}
        ${DARK_BEAM_VERTEX_SPV}
        ${DARK_BEAM_FRAGMENT_SPV}
)

add_dependencies(dark dark_shaders)
target_compile_definitions(dark PRIVATE
    DARK_SHADER_DIR="${DARK_SHADER_OUTPUT_DIR}"
    DARK_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

foreach(_asset
    ${CMAKE_CURRENT_SOURCE_DIR}/data/pyramid.obj
    ${CMAKE_CURRENT_SOURCE_DIR}/data/beam.obj
    ${CMAKE_SOURCE_DIR}/examples/text_example/data/font.ttf
)
    if(NOT EXISTS ${_asset})
        message(FATAL_ERROR "dark missing required asset: ${_asset}")
    endif()
endforeach()

add_custom_command(TARGET dark POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:dark>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/pyramid.obj
        $<TARGET_FILE_DIR:dark>/data/pyramid.obj
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/beam.obj
        $<TARGET_FILE_DIR:dark>/data/beam.obj
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/text_example/data/font.ttf
        $<TARGET_FILE_DIR:dark>/data/font.ttf
    COMMENT "Copying dark assets to runtime output directory"
)
