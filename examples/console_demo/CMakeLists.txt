add_executable(console_demo
    ${CMAKE_SOURCE_DIR}/examples/console_demo/main.cpp
)

target_link_libraries(console_demo PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(console_demo_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(console_demo_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(console_demo_SPRITE_VERTEX_SHADER ${console_demo_SHADER_SOURCE_DIR}/sprite.vert)
set(console_demo_SPRITE_VERTEX_SPV ${console_demo_SHADER_OUTPUT_DIR}/sprite.vert.spv)
set(console_demo_PULSE_FRAGMENT_SHADER ${console_demo_SHADER_SOURCE_DIR}/background_pulse.frag)
set(console_demo_PULSE_FRAGMENT_SPV ${console_demo_SHADER_OUTPUT_DIR}/background_pulse.frag.spv)

add_custom_command(
    OUTPUT ${console_demo_SPRITE_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${console_demo_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${console_demo_SPRITE_VERTEX_SHADER} -o ${console_demo_SPRITE_VERTEX_SPV}
    DEPENDS ${console_demo_SPRITE_VERTEX_SHADER}
    COMMENT "Compiling console demo sprite vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${console_demo_PULSE_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${console_demo_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${console_demo_PULSE_FRAGMENT_SHADER} -o ${console_demo_PULSE_FRAGMENT_SPV}
    DEPENDS ${console_demo_PULSE_FRAGMENT_SHADER}
    COMMENT "Compiling console demo pulse fragment shader"
    VERBATIM
)

add_custom_target(console_demo_shaders
    DEPENDS
        ${console_demo_SPRITE_VERTEX_SPV}
        ${console_demo_PULSE_FRAGMENT_SPV}
)

add_dependencies(console_demo console_demo_shaders)

target_compile_definitions(console_demo PRIVATE
    console_demo_ASSET_DIR="$<TARGET_FILE_DIR:console_demo>"
)

set(console_demo_DATA_DIR ${CMAKE_SOURCE_DIR}/examples/console_demo/data)
set(console_demo_FONT_SOURCE ${console_demo_DATA_DIR}/font.ttf)
set(console_demo_BACKGROUND_SOURCE ${console_demo_DATA_DIR}/background.png)

add_custom_command(TARGET console_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:console_demo>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${console_demo_FONT_SOURCE}
        $<TARGET_FILE_DIR:console_demo>/data/font.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${console_demo_BACKGROUND_SOURCE}
        $<TARGET_FILE_DIR:console_demo>/data/background.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${console_demo_SPRITE_VERTEX_SPV}
        $<TARGET_FILE_DIR:console_demo>/data/sprite.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${console_demo_PULSE_FRAGMENT_SPV}
        $<TARGET_FILE_DIR:console_demo>/data/background_pulse.frag.spv
    COMMENT "Copying console demo runtime assets to output directory"
)
