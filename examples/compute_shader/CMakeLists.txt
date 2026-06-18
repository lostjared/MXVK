add_executable(compute_shader
    ${CMAKE_SOURCE_DIR}/examples/compute_shader/main.cpp
)

target_link_libraries(compute_shader PRIVATE mxvk::mxvk)
if(TARGET mxwrite)
    target_link_libraries(compute_shader PRIVATE mxwrite)
endif()

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(compute_shader_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(compute_shader_SHADER_OUTPUT_DIR $<TARGET_FILE_DIR:compute_shader>)
set(compute_shader_INDEX_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/index.txt)
set(compute_shader_FONT_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/font.ttf)

set(compute_shader_COMPUTE_SHADER ${compute_shader_SHADER_SOURCE_DIR}/compute.comp)
set(compute_shader_COMPUTE_SPV ${CMAKE_CURRENT_BINARY_DIR}/compute.spv)
set(compute_shader_XORBLEND_SHADER ${compute_shader_SHADER_SOURCE_DIR}/xorblend.comp)
set(compute_shader_XORBLEND_SPV ${CMAKE_CURRENT_BINARY_DIR}/xorblend.spv)
set(compute_shader_MEDIAN_SHADER ${compute_shader_SHADER_SOURCE_DIR}/metalmedianblend.comp)
set(compute_shader_MEDIAN_SPV ${CMAKE_CURRENT_BINARY_DIR}/metalmedianblend.spv)
set(compute_shader_SQUARE_SHADER ${compute_shader_SHADER_SOURCE_DIR}/square_block_resize_dir.comp)
set(compute_shader_SQUARE_SPV ${CMAKE_CURRENT_BINARY_DIR}/square_block_resize_dir.spv)
set(compute_shader_ACIDCAM_SHADER ${compute_shader_SHADER_SOURCE_DIR}/acidcam_filters.comp)
set(compute_shader_ACIDCAM_SPV ${CMAKE_CURRENT_BINARY_DIR}/acidcam_filters.spv)

add_custom_command(
    OUTPUT ${compute_shader_COMPUTE_SPV}
    COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=compute ${compute_shader_COMPUTE_SHADER} -o ${compute_shader_COMPUTE_SPV}
    DEPENDS ${compute_shader_COMPUTE_SHADER}
    COMMENT "Compiling compute_shader compute.comp"
    VERBATIM
)

add_custom_command(
    OUTPUT ${compute_shader_XORBLEND_SPV}
    COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=compute ${compute_shader_XORBLEND_SHADER} -o ${compute_shader_XORBLEND_SPV}
    DEPENDS ${compute_shader_XORBLEND_SHADER}
    COMMENT "Compiling compute_shader xorblend.comp"
    VERBATIM
)

add_custom_command(
    OUTPUT ${compute_shader_MEDIAN_SPV}
    COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=compute ${compute_shader_MEDIAN_SHADER} -o ${compute_shader_MEDIAN_SPV}
    DEPENDS ${compute_shader_MEDIAN_SHADER}
    COMMENT "Compiling compute_shader metalmedianblend.comp"
    VERBATIM
)

add_custom_command(
    OUTPUT ${compute_shader_SQUARE_SPV}
    COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=compute ${compute_shader_SQUARE_SHADER} -o ${compute_shader_SQUARE_SPV}
    DEPENDS ${compute_shader_SQUARE_SHADER}
    COMMENT "Compiling compute_shader square_block_resize_dir.comp"
    VERBATIM
)

add_custom_command(
    OUTPUT ${compute_shader_ACIDCAM_SPV}
    COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=compute ${compute_shader_ACIDCAM_SHADER} -o ${compute_shader_ACIDCAM_SPV}
    DEPENDS ${compute_shader_ACIDCAM_SHADER}
    COMMENT "Compiling compute_shader acidcam_filters.comp"
    VERBATIM
)

add_custom_target(compute_shader_shaders
    DEPENDS
        ${compute_shader_COMPUTE_SPV}
        ${compute_shader_XORBLEND_SPV}
        ${compute_shader_MEDIAN_SPV}
        ${compute_shader_SQUARE_SPV}
        ${compute_shader_ACIDCAM_SPV}
)

add_dependencies(compute_shader compute_shader_shaders)

target_compile_definitions(compute_shader PRIVATE
    compute_shader_ASSET_DIR="$<TARGET_FILE_DIR:compute_shader>"
)

add_custom_command(TARGET compute_shader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${compute_shader_INDEX_SOURCE}
        $<TARGET_FILE_DIR:compute_shader>/index.txt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${compute_shader_FONT_SOURCE}
        $<TARGET_FILE_DIR:compute_shader>/font.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${compute_shader_COMPUTE_SPV}
        $<TARGET_FILE_DIR:compute_shader>/compute.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${compute_shader_XORBLEND_SPV}
        $<TARGET_FILE_DIR:compute_shader>/xorblend.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${compute_shader_MEDIAN_SPV}
        $<TARGET_FILE_DIR:compute_shader>/metalmedianblend.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${compute_shader_SQUARE_SPV}
        $<TARGET_FILE_DIR:compute_shader>/square_block_resize_dir.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${compute_shader_ACIDCAM_SPV}
        $<TARGET_FILE_DIR:compute_shader>/acidcam_filters.spv
    COMMENT "Copying compute_shader runtime assets"
)
