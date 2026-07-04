add_executable(mutatris
    ${CMAKE_SOURCE_DIR}/examples/mutatris/mutatris.cpp
)

target_link_libraries(mutatris PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(MUTATRIS_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(MUTATRIS_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(MUTATRIS_BACKGROUND_VERTEX_SHADER ${CMAKE_SOURCE_DIR}/mxvk/shaders/sprite.vert)
set(MUTATRIS_BACKGROUND_FRAGMENT_SHADER ${MUTATRIS_SHADER_SOURCE_DIR}/background.frag)
set(MUTATRIS_FADE_FRAGMENT_SHADER ${MUTATRIS_SHADER_SOURCE_DIR}/fade.frag)
set(MUTATRIS_BACKGROUND_VERTEX_SPV ${MUTATRIS_SHADER_OUTPUT_DIR}/background.vert.spv)
set(MUTATRIS_BACKGROUND_FRAGMENT_SPV ${MUTATRIS_SHADER_OUTPUT_DIR}/background.frag.spv)
set(MUTATRIS_FADE_FRAGMENT_SPV ${MUTATRIS_SHADER_OUTPUT_DIR}/fade.frag.spv)

add_custom_command(
    OUTPUT ${MUTATRIS_BACKGROUND_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MUTATRIS_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${MUTATRIS_BACKGROUND_VERTEX_SHADER} -o ${MUTATRIS_BACKGROUND_VERTEX_SPV}
    DEPENDS ${MUTATRIS_BACKGROUND_VERTEX_SHADER}
    COMMENT "Compiling mutatris background vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${MUTATRIS_BACKGROUND_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MUTATRIS_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${MUTATRIS_BACKGROUND_FRAGMENT_SHADER} -o ${MUTATRIS_BACKGROUND_FRAGMENT_SPV}
    DEPENDS ${MUTATRIS_BACKGROUND_FRAGMENT_SHADER}
    COMMENT "Compiling mutatris background fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${MUTATRIS_FADE_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MUTATRIS_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${MUTATRIS_FADE_FRAGMENT_SHADER} -o ${MUTATRIS_FADE_FRAGMENT_SPV}
    DEPENDS ${MUTATRIS_FADE_FRAGMENT_SHADER}
    COMMENT "Compiling mutatris fade fragment shader"
    VERBATIM
)

add_custom_target(mutatris_shaders
    DEPENDS
        ${MUTATRIS_BACKGROUND_VERTEX_SPV}
        ${MUTATRIS_BACKGROUND_FRAGMENT_SPV}
        ${MUTATRIS_FADE_FRAGMENT_SPV}
)

add_dependencies(mutatris mutatris_shaders)

target_compile_definitions(mutatris PRIVATE
    mutatris_ASSET_DIR="$<TARGET_FILE_DIR:mutatris>"
    mutatris_FONT_PATH="$<TARGET_FILE_DIR:mutatris>/data/font.ttf"
    mutatris_SHADER_DIR="${MUTATRIS_SHADER_OUTPUT_DIR}"
)

add_custom_command(TARGET mutatris POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:mutatris>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:mutatris>/data
    COMMENT "Copying mutatris assets to runtime output directory"
)
