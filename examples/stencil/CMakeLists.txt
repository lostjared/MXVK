add_executable(stencil
    ${CMAKE_SOURCE_DIR}/examples/stencil/stencil.cpp
)
target_link_libraries(stencil PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(STENCIL_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(STENCIL_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(STENCIL_RUNTIME_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)

set(STENCIL_MASK_VERTEX_SHADER ${STENCIL_SHADER_SOURCE_DIR}/fullscreen.vert)
set(STENCIL_MASK_FRAGMENT_SHADER ${STENCIL_SHADER_SOURCE_DIR}/mandala_mask.frag)
set(STENCIL_CONTENT_VERTEX_SHADER ${STENCIL_SHADER_SOURCE_DIR}/fullscreen.vert)
set(STENCIL_CONTENT_FRAGMENT_SHADER ${STENCIL_SHADER_SOURCE_DIR}/stencil_fill.frag)

set(STENCIL_MASK_VERTEX_SPV ${STENCIL_SHADER_OUTPUT_DIR}/fullscreen.vert.spv)
set(STENCIL_MASK_FRAGMENT_SPV ${STENCIL_SHADER_OUTPUT_DIR}/mandala_mask.frag.spv)
set(STENCIL_CONTENT_VERTEX_SPV ${STENCIL_SHADER_OUTPUT_DIR}/content_fullscreen.vert.spv)
set(STENCIL_CONTENT_FRAGMENT_SPV ${STENCIL_SHADER_OUTPUT_DIR}/stencil_fill.frag.spv)

add_custom_command(
    OUTPUT ${STENCIL_MASK_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${STENCIL_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${STENCIL_MASK_VERTEX_SHADER} -o ${STENCIL_MASK_VERTEX_SPV}
    DEPENDS ${STENCIL_MASK_VERTEX_SHADER}
    COMMENT "Compiling stencil fullscreen vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${STENCIL_MASK_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${STENCIL_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${STENCIL_MASK_FRAGMENT_SHADER} -o ${STENCIL_MASK_FRAGMENT_SPV}
    DEPENDS ${STENCIL_MASK_FRAGMENT_SHADER}
    COMMENT "Compiling stencil mandala mask shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${STENCIL_CONTENT_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${STENCIL_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${STENCIL_CONTENT_VERTEX_SHADER} -o ${STENCIL_CONTENT_VERTEX_SPV}
    DEPENDS ${STENCIL_CONTENT_VERTEX_SHADER}
    COMMENT "Compiling stencil content vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${STENCIL_CONTENT_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${STENCIL_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${STENCIL_CONTENT_FRAGMENT_SHADER} -o ${STENCIL_CONTENT_FRAGMENT_SPV}
    DEPENDS ${STENCIL_CONTENT_FRAGMENT_SHADER}
    COMMENT "Compiling stencil fill shader"
    VERBATIM
)

add_custom_target(stencil_shaders
    COMMAND ${CMAKE_COMMAND} -E make_directory ${STENCIL_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${STENCIL_MASK_VERTEX_SPV} ${STENCIL_RUNTIME_DATA_DIR}/fullscreen.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${STENCIL_MASK_FRAGMENT_SPV} ${STENCIL_RUNTIME_DATA_DIR}/mandala_mask.frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${STENCIL_CONTENT_VERTEX_SPV} ${STENCIL_RUNTIME_DATA_DIR}/content_fullscreen.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${STENCIL_CONTENT_FRAGMENT_SPV} ${STENCIL_RUNTIME_DATA_DIR}/stencil_fill.frag.spv
    DEPENDS
        ${STENCIL_MASK_VERTEX_SPV}
        ${STENCIL_MASK_FRAGMENT_SPV}
        ${STENCIL_CONTENT_VERTEX_SPV}
        ${STENCIL_CONTENT_FRAGMENT_SPV}
    COMMENT "Copying stencil SPIR-V shaders to runtime data directory"
    VERBATIM
)

add_dependencies(stencil stencil_shaders)
target_compile_definitions(stencil PRIVATE
    STENCIL_ASSET_DIR="$<TARGET_FILE_DIR:stencil>"
)
