add_executable(knight
    ${CMAKE_SOURCE_DIR}/examples/knight/knight.cpp
)
target_link_libraries(knight PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(KNIGHT_FADE_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fade.frag)
set(KNIGHT_FADE_SPV ${CMAKE_CURRENT_BINARY_DIR}/shaders/fade.frag.spv)
set(KNIGHT_COLOR_KEY_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/color_key.frag)
set(KNIGHT_COLOR_KEY_SPV ${CMAKE_CURRENT_BINARY_DIR}/shaders/color_key.frag.spv)

add_custom_command(
    OUTPUT ${KNIGHT_FADE_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
    COMMAND ${GLSLC_EXECUTABLE} ${KNIGHT_FADE_SHADER} -o ${KNIGHT_FADE_SPV}
    DEPENDS ${KNIGHT_FADE_SHADER}
    COMMENT "Compiling knight intro fade shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${KNIGHT_COLOR_KEY_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
    COMMAND ${GLSLC_EXECUTABLE} ${KNIGHT_COLOR_KEY_SHADER} -o ${KNIGHT_COLOR_KEY_SPV}
    DEPENDS ${KNIGHT_COLOR_KEY_SHADER}
    COMMENT "Compiling knight color-key shader"
    VERBATIM
)

add_custom_target(knight_shaders DEPENDS
    ${KNIGHT_FADE_SPV}
    ${KNIGHT_COLOR_KEY_SPV}
)
add_dependencies(knight knight_shaders)

target_compile_definitions(knight PRIVATE
    KNIGHT_ASSET_DIR="$<TARGET_FILE_DIR:knight>"
)

add_custom_command(TARGET knight POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:knight>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/font.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/data/knight.png
        ${CMAKE_CURRENT_SOURCE_DIR}/data/logo.png
        ${KNIGHT_FADE_SPV}
        ${KNIGHT_COLOR_KEY_SPV}
        $<TARGET_FILE_DIR:knight>/data
    COMMENT "Copying knight runtime data"
    VERBATIM
)
