add_executable(breakout
    ${CMAKE_SOURCE_DIR}/examples/breakout/breakout.cpp
)

target_link_libraries(breakout PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(BREAKOUT_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(BREAKOUT_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(BREAKOUT_VERTEX_SHADER ${BREAKOUT_SHADER_SOURCE_DIR}/model.vert)
set(BREAKOUT_FRAGMENT_SHADER ${BREAKOUT_SHADER_SOURCE_DIR}/model.frag)
set(BREAKOUT_BACKGROUND_FRAGMENT_SHADER ${BREAKOUT_SHADER_SOURCE_DIR}/background.frag)
set(BREAKOUT_VERTEX_SPV ${BREAKOUT_SHADER_OUTPUT_DIR}/breakout_model.vert.spv)
set(BREAKOUT_FRAGMENT_SPV ${BREAKOUT_SHADER_OUTPUT_DIR}/breakout_model.frag.spv)
set(BREAKOUT_BACKGROUND_FRAGMENT_SPV ${BREAKOUT_SHADER_OUTPUT_DIR}/breakout_background.frag.spv)

add_custom_command(
    OUTPUT ${BREAKOUT_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BREAKOUT_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${BREAKOUT_VERTEX_SHADER} -o ${BREAKOUT_VERTEX_SPV}
    DEPENDS ${BREAKOUT_VERTEX_SHADER}
    COMMENT "Compiling breakout vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${BREAKOUT_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BREAKOUT_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${BREAKOUT_FRAGMENT_SHADER} -o ${BREAKOUT_FRAGMENT_SPV}
    DEPENDS ${BREAKOUT_FRAGMENT_SHADER}
    COMMENT "Compiling breakout fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${BREAKOUT_BACKGROUND_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BREAKOUT_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${BREAKOUT_BACKGROUND_FRAGMENT_SHADER} -o ${BREAKOUT_BACKGROUND_FRAGMENT_SPV}
    DEPENDS ${BREAKOUT_BACKGROUND_FRAGMENT_SHADER}
    COMMENT "Compiling breakout background fragment shader"
    VERBATIM
)

add_custom_target(breakout_shaders
    DEPENDS
        ${BREAKOUT_VERTEX_SPV}
        ${BREAKOUT_FRAGMENT_SPV}
        ${BREAKOUT_BACKGROUND_FRAGMENT_SPV}
)

add_dependencies(breakout breakout_shaders)

target_compile_definitions(breakout PRIVATE
    breakout_ASSET_DIR="$<TARGET_FILE_DIR:breakout>"
    breakout_SHADER_DIR="${BREAKOUT_SHADER_OUTPUT_DIR}"
)

add_custom_command(TARGET breakout POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:breakout>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:breakout>/data
    COMMENT "Copying breakout assets to runtime output directory"
)
