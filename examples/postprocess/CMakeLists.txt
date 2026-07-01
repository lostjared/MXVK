add_executable(postprocess
    ${CMAKE_SOURCE_DIR}/examples/postprocess/post.cpp
)

target_link_libraries(postprocess PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(postprocess_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(postprocess_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(postprocess_DATA_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)

set(postprocess_INVERT_FRAGMENT_SHADER ${postprocess_SHADER_SOURCE_DIR}/invert.frag)
set(postprocess_SCANLINE_FRAGMENT_SHADER ${postprocess_SHADER_SOURCE_DIR}/scanline.frag)
set(postprocess_INVERT_FRAGMENT_SPV ${postprocess_SHADER_OUTPUT_DIR}/invert.frag.spv)
set(postprocess_SCANLINE_FRAGMENT_SPV ${postprocess_SHADER_OUTPUT_DIR}/scanline.frag.spv)

add_custom_command(
    OUTPUT ${postprocess_INVERT_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${postprocess_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${postprocess_INVERT_FRAGMENT_SHADER} -o ${postprocess_INVERT_FRAGMENT_SPV}
    DEPENDS ${postprocess_INVERT_FRAGMENT_SHADER}
    COMMENT "Compiling postprocess invert fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${postprocess_SCANLINE_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${postprocess_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${postprocess_SCANLINE_FRAGMENT_SHADER} -o ${postprocess_SCANLINE_FRAGMENT_SPV}
    DEPENDS ${postprocess_SCANLINE_FRAGMENT_SHADER}
    COMMENT "Compiling postprocess scanline fragment shader"
    VERBATIM
)

add_custom_target(postprocess_shaders
    DEPENDS
        ${postprocess_INVERT_FRAGMENT_SPV}
        ${postprocess_SCANLINE_FRAGMENT_SPV}
)

add_dependencies(postprocess postprocess_shaders)

target_compile_definitions(postprocess PRIVATE
    postprocess_ASSET_DIR="$<TARGET_FILE_DIR:postprocess>"
)

add_custom_command(TARGET postprocess POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:postprocess>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${postprocess_DATA_SOURCE_DIR}/shaders.txt
        $<TARGET_FILE_DIR:postprocess>/data/shaders.txt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${postprocess_INVERT_FRAGMENT_SPV}
        $<TARGET_FILE_DIR:postprocess>/data/invert.frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${postprocess_SCANLINE_FRAGMENT_SPV}
        $<TARGET_FILE_DIR:postprocess>/data/scanline.frag.spv
    COMMENT "Copying postprocess runtime data"
)
