add_executable(asteroids3d
    ${CMAKE_SOURCE_DIR}/examples/asteroids3d/main.cpp
    ${CMAKE_SOURCE_DIR}/examples/asteroids3d/asteroids3d_window.cpp
    ${CMAKE_SOURCE_DIR}/examples/asteroids3d/ship.cpp
    ${CMAKE_SOURCE_DIR}/examples/asteroids3d/starfield.cpp
)
target_link_libraries(asteroids3d PRIVATE rain)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(ASTEROIDS3D_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(ASTEROIDS3D_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

set(ASTEROIDS3D_MODEL_VERTEX_SHADER ${ASTEROIDS3D_SHADER_SOURCE_DIR}/model.vert)
set(ASTEROIDS3D_MODEL_FRAGMENT_SHADER ${ASTEROIDS3D_SHADER_SOURCE_DIR}/model.frag)
set(ASTEROIDS3D_INTRO_FRAGMENT_SHADER ${ASTEROIDS3D_SHADER_SOURCE_DIR}/intro.frag)
set(ASTEROIDS3D_CRT_FRAGMENT_SHADER ${ASTEROIDS3D_SHADER_SOURCE_DIR}/crt.frag)
set(ASTEROIDS3D_FLAME_VERTEX_SHADER ${ASTEROIDS3D_SHADER_SOURCE_DIR}/flame.vert)
set(ASTEROIDS3D_FLAME_FRAGMENT_SHADER ${ASTEROIDS3D_SHADER_SOURCE_DIR}/flame.frag)
set(ASTEROIDS3D_MODEL_VERTEX_SPV ${ASTEROIDS3D_SHADER_OUTPUT_DIR}/model.vert.spv)
set(ASTEROIDS3D_MODEL_FRAGMENT_SPV ${ASTEROIDS3D_SHADER_OUTPUT_DIR}/model.frag.spv)
set(ASTEROIDS3D_INTRO_FRAGMENT_SPV ${ASTEROIDS3D_SHADER_OUTPUT_DIR}/intro.frag.spv)
set(ASTEROIDS3D_CRT_FRAGMENT_SPV ${ASTEROIDS3D_SHADER_OUTPUT_DIR}/crt.frag.spv)
set(ASTEROIDS3D_FLAME_VERTEX_SPV ${ASTEROIDS3D_SHADER_OUTPUT_DIR}/flame.vert.spv)
set(ASTEROIDS3D_FLAME_FRAGMENT_SPV ${ASTEROIDS3D_SHADER_OUTPUT_DIR}/flame.frag.spv)

add_custom_command(
    OUTPUT ${ASTEROIDS3D_MODEL_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ASTEROIDS3D_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${ASTEROIDS3D_MODEL_VERTEX_SHADER} -o ${ASTEROIDS3D_MODEL_VERTEX_SPV}
    DEPENDS ${ASTEROIDS3D_MODEL_VERTEX_SHADER}
    COMMENT "Compiling asteroids3d model vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${ASTEROIDS3D_MODEL_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ASTEROIDS3D_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${ASTEROIDS3D_MODEL_FRAGMENT_SHADER} -o ${ASTEROIDS3D_MODEL_FRAGMENT_SPV}
    DEPENDS ${ASTEROIDS3D_MODEL_FRAGMENT_SHADER}
    COMMENT "Compiling asteroids3d model fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${ASTEROIDS3D_INTRO_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ASTEROIDS3D_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${ASTEROIDS3D_INTRO_FRAGMENT_SHADER} -o ${ASTEROIDS3D_INTRO_FRAGMENT_SPV}
    DEPENDS ${ASTEROIDS3D_INTRO_FRAGMENT_SHADER}
    COMMENT "Compiling asteroids3d intro fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${ASTEROIDS3D_CRT_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ASTEROIDS3D_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${ASTEROIDS3D_CRT_FRAGMENT_SHADER} -o ${ASTEROIDS3D_CRT_FRAGMENT_SPV}
    DEPENDS ${ASTEROIDS3D_CRT_FRAGMENT_SHADER}
    COMMENT "Compiling asteroids3d CRT fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${ASTEROIDS3D_FLAME_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ASTEROIDS3D_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${ASTEROIDS3D_FLAME_VERTEX_SHADER} -o ${ASTEROIDS3D_FLAME_VERTEX_SPV}
    DEPENDS ${ASTEROIDS3D_FLAME_VERTEX_SHADER}
    COMMENT "Compiling asteroids3d flame vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${ASTEROIDS3D_FLAME_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ASTEROIDS3D_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${ASTEROIDS3D_FLAME_FRAGMENT_SHADER} -o ${ASTEROIDS3D_FLAME_FRAGMENT_SPV}
    DEPENDS ${ASTEROIDS3D_FLAME_FRAGMENT_SHADER}
    COMMENT "Compiling asteroids3d flame fragment shader"
    VERBATIM
)

add_custom_target(asteroids3d_shaders
    DEPENDS
        ${ASTEROIDS3D_MODEL_VERTEX_SPV}
        ${ASTEROIDS3D_MODEL_FRAGMENT_SPV}
        ${ASTEROIDS3D_INTRO_FRAGMENT_SPV}
        ${ASTEROIDS3D_CRT_FRAGMENT_SPV}
        ${ASTEROIDS3D_FLAME_VERTEX_SPV}
        ${ASTEROIDS3D_FLAME_FRAGMENT_SPV}
)

add_dependencies(asteroids3d asteroids3d_shaders)
target_compile_definitions(asteroids3d PRIVATE
    ASTEROIDS3D_SHADER_DIR="${ASTEROIDS3D_SHADER_OUTPUT_DIR}"
    ASTEROIDS3D_ASSET_DIR="$<TARGET_FILE_DIR:asteroids3d>"
    ASTEROIDS3D_SOURCE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    ASTEROIDS3D_DEFENDER_SOUND_DIR="${CMAKE_SOURCE_DIR}/examples/defender/data"
)

add_custom_command(TARGET asteroids3d POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:asteroids3d>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:asteroids3d>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/rain/data/NotoSansCJK-Bold.ttc
        $<TARGET_FILE_DIR:asteroids3d>/data/NotoSansCJK-Bold.ttc
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/rain/data/NotoSansCJK-Regular.ttc
        $<TARGET_FILE_DIR:asteroids3d>/data/NotoSansCJK-Regular.ttc
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/music.ogg
        $<TARGET_FILE_DIR:asteroids3d>/data/music.ogg
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/cannon.wav
        $<TARGET_FILE_DIR:asteroids3d>/data/cannon.wav
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/defender/data/crash.wav
        ${CMAKE_SOURCE_DIR}/examples/defender/data/asteroid.wav
        $<TARGET_FILE_DIR:asteroids3d>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/asteroid.obj
        $<TARGET_FILE_DIR:asteroids3d>/data/asteroid.obj
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/asteroid2.obj
        $<TARGET_FILE_DIR:asteroids3d>/data/asteroid2.obj
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/asteroid3.obj
        $<TARGET_FILE_DIR:asteroids3d>/data/asteroid3.obj
    COMMENT "Copying asteroids3d assets to runtime output directory"
)
