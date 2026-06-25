add_executable(defender
    ${CMAKE_SOURCE_DIR}/examples/defender/defender.cpp
    ${CMAKE_SOURCE_DIR}/examples/asteroids3d/ship.cpp
    ${CMAKE_SOURCE_DIR}/examples/asteroids3d/starfield.cpp
)
target_link_libraries(defender PRIVATE mxvk::mxvk)
target_link_libraries(defender PRIVATE rain)
target_include_directories(defender PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/asteroids3d
)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(DEFENDER_SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/examples/asteroids3d/shaders)
set(DEFENDER_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(DEFENDER_MODEL_VERTEX_SHADER ${DEFENDER_SHADER_SOURCE_DIR}/model.vert)
set(DEFENDER_MODEL_FRAGMENT_SHADER ${DEFENDER_SHADER_SOURCE_DIR}/model.frag)
set(DEFENDER_FLAME_VERTEX_SHADER ${DEFENDER_SHADER_SOURCE_DIR}/flame.vert)
set(DEFENDER_FLAME_FRAGMENT_SHADER ${DEFENDER_SHADER_SOURCE_DIR}/flame.frag)
set(DEFENDER_INTRO_FRAGMENT_SHADER ${DEFENDER_SHADER_SOURCE_DIR}/intro.frag)
set(DEFENDER_FADE_OVERLAY_FRAGMENT_SHADER ${DEFENDER_SHADER_SOURCE_DIR}/fade_overlay.frag)
set(DEFENDER_MODEL_VERTEX_SPV ${DEFENDER_SHADER_OUTPUT_DIR}/model.vert.spv)
set(DEFENDER_MODEL_FRAGMENT_SPV ${DEFENDER_SHADER_OUTPUT_DIR}/model.frag.spv)
set(DEFENDER_FLAME_VERTEX_SPV ${DEFENDER_SHADER_OUTPUT_DIR}/flame.vert.spv)
set(DEFENDER_FLAME_FRAGMENT_SPV ${DEFENDER_SHADER_OUTPUT_DIR}/flame.frag.spv)
set(DEFENDER_INTRO_FRAGMENT_SPV ${DEFENDER_SHADER_OUTPUT_DIR}/intro.frag.spv)
set(DEFENDER_FADE_OVERLAY_FRAGMENT_SPV ${DEFENDER_SHADER_OUTPUT_DIR}/fade_overlay.frag.spv)

add_custom_command(
    OUTPUT ${DEFENDER_MODEL_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DEFENDER_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${DEFENDER_MODEL_VERTEX_SHADER} -o ${DEFENDER_MODEL_VERTEX_SPV}
    DEPENDS ${DEFENDER_MODEL_VERTEX_SHADER}
    COMMENT "Compiling defender model vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${DEFENDER_MODEL_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DEFENDER_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${DEFENDER_MODEL_FRAGMENT_SHADER} -o ${DEFENDER_MODEL_FRAGMENT_SPV}
    DEPENDS ${DEFENDER_MODEL_FRAGMENT_SHADER}
    COMMENT "Compiling defender model fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${DEFENDER_FLAME_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DEFENDER_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${DEFENDER_FLAME_VERTEX_SHADER} -o ${DEFENDER_FLAME_VERTEX_SPV}
    DEPENDS ${DEFENDER_FLAME_VERTEX_SHADER}
    COMMENT "Compiling defender flame vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${DEFENDER_FLAME_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DEFENDER_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${DEFENDER_FLAME_FRAGMENT_SHADER} -o ${DEFENDER_FLAME_FRAGMENT_SPV}
    DEPENDS ${DEFENDER_FLAME_FRAGMENT_SHADER}
    COMMENT "Compiling defender flame fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${DEFENDER_INTRO_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DEFENDER_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${DEFENDER_INTRO_FRAGMENT_SHADER} -o ${DEFENDER_INTRO_FRAGMENT_SPV}
    DEPENDS ${DEFENDER_INTRO_FRAGMENT_SHADER}
    COMMENT "Compiling defender intro fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${DEFENDER_FADE_OVERLAY_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DEFENDER_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${DEFENDER_FADE_OVERLAY_FRAGMENT_SHADER} -o ${DEFENDER_FADE_OVERLAY_FRAGMENT_SPV}
    DEPENDS ${DEFENDER_FADE_OVERLAY_FRAGMENT_SHADER}
    COMMENT "Compiling defender fade overlay fragment shader"
    VERBATIM
)

add_custom_target(defender_shaders
    DEPENDS
        ${DEFENDER_MODEL_VERTEX_SPV}
        ${DEFENDER_MODEL_FRAGMENT_SPV}
        ${DEFENDER_FLAME_VERTEX_SPV}
        ${DEFENDER_FLAME_FRAGMENT_SPV}
        ${DEFENDER_INTRO_FRAGMENT_SPV}
        ${DEFENDER_FADE_OVERLAY_FRAGMENT_SPV}
)

add_dependencies(defender defender_shaders)
target_compile_definitions(defender PRIVATE
    DEFENDER_SHADER_DIR="${DEFENDER_SHADER_OUTPUT_DIR}"
    DEFENDER_ASSET_DIR="$<TARGET_FILE_DIR:defender>"
)

set(DEFENDER_DATA_DIR ${CMAKE_SOURCE_DIR}/examples/asteroids3d/data)
set(DEFENDER_LOCAL_DATA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(DEFENDER_UFO_FRAME_ASSETS
    ${DEFENDER_LOCAL_DATA_DIR}/ufo_lights_00.png
    ${DEFENDER_LOCAL_DATA_DIR}/ufo_lights_01.png
    ${DEFENDER_LOCAL_DATA_DIR}/ufo_lights_02.png
    ${DEFENDER_LOCAL_DATA_DIR}/ufo_lights_03.png
    ${DEFENDER_LOCAL_DATA_DIR}/ufo_lights_04.png
    ${DEFENDER_LOCAL_DATA_DIR}/ufo_lights_05.png
    ${DEFENDER_LOCAL_DATA_DIR}/ufo_lights_06.png
    ${DEFENDER_LOCAL_DATA_DIR}/ufo_lights_07.png
    ${DEFENDER_LOCAL_DATA_DIR}/ufox1.png
    ${DEFENDER_LOCAL_DATA_DIR}/ufox2.png
    ${DEFENDER_LOCAL_DATA_DIR}/ufox3.png
    ${DEFENDER_LOCAL_DATA_DIR}/ufox4.png
    ${DEFENDER_LOCAL_DATA_DIR}/ufox5.png
    ${DEFENDER_LOCAL_DATA_DIR}/ufox6.png
    ${DEFENDER_LOCAL_DATA_DIR}/ufox7.png
    ${DEFENDER_LOCAL_DATA_DIR}/ufox8.png
)
set(DEFENDER_REQUIRED_ASSETS
    ${DEFENDER_DATA_DIR}/starship.obj
    ${DEFENDER_DATA_DIR}/starship.mtl
    ${DEFENDER_LOCAL_DATA_DIR}/asteroid.obj
    ${DEFENDER_LOCAL_DATA_DIR}/asteroid.mtl
    ${DEFENDER_LOCAL_DATA_DIR}/rock.png
    ${DEFENDER_LOCAL_DATA_DIR}/intro.png
    ${DEFENDER_LOCAL_DATA_DIR}/background.ogg
    ${DEFENDER_LOCAL_DATA_DIR}/crash.wav
    ${DEFENDER_LOCAL_DATA_DIR}/shoot.wav
    ${DEFENDER_LOCAL_DATA_DIR}/takeoff.wav
    ${DEFENDER_LOCAL_DATA_DIR}/asteroid.wav
    ${DEFENDER_LOCAL_DATA_DIR}/ufo.wav
    ${DEFENDER_DATA_DIR}/star.png
    ${DEFENDER_DATA_DIR}/particle_projectile.png
    ${DEFENDER_DATA_DIR}/particle_explosion.png
    ${DEFENDER_DATA_DIR}/font.ttf
    ${CMAKE_SOURCE_DIR}/examples/rain/data/NotoSansCJK-Bold.ttc
    ${CMAKE_SOURCE_DIR}/examples/rain/data/NotoSansCJK-Regular.ttc
    ${DEFENDER_UFO_FRAME_ASSETS}
    ${DEFENDER_DATA_DIR}/rear_cap_metal.png
    ${DEFENDER_DATA_DIR}/plane.png
    ${DEFENDER_DATA_DIR}/ship_cockpit.png
    ${DEFENDER_DATA_DIR}/ship_cockpit_red.png
    ${DEFENDER_DATA_DIR}/ship_fin.png
    ${DEFENDER_DATA_DIR}/ship_engine.png
    ${DEFENDER_DATA_DIR}/ship_engine_side.png
    ${DEFENDER_DATA_DIR}/ship_hull.png
    ${DEFENDER_DATA_DIR}/ship_wing.png
)

foreach(_asset ${DEFENDER_REQUIRED_ASSETS})
    if(NOT EXISTS ${_asset})
        message(FATAL_ERROR "defender missing required asset: ${_asset}")
    endif()
endforeach()

add_custom_command(TARGET defender POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:defender>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${DEFENDER_REQUIRED_ASSETS}
        $<TARGET_FILE_DIR:defender>/data
    COMMENT "Copying defender assets"
)
