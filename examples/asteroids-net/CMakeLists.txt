add_executable(asteroids-net
    main.cpp
    multiplayer.cpp
    port_mapping.cpp
    asteroids3d_window.cpp
    ship.cpp
    starfield.cpp
)
target_link_libraries(asteroids-net PRIVATE rain libmxnetwork::mxnetwork)

find_path(MINIUPNPC_INCLUDE_DIR miniupnpc/miniupnpc.h)
find_library(MINIUPNPC_LIBRARY NAMES miniupnpc)
if(MINIUPNPC_INCLUDE_DIR AND MINIUPNPC_LIBRARY)
    target_include_directories(asteroids-net PRIVATE ${MINIUPNPC_INCLUDE_DIR})
    target_link_libraries(asteroids-net PRIVATE ${MINIUPNPC_LIBRARY})
    target_compile_definitions(asteroids-net PRIVATE ASTEROIDS_NET_HAS_MINIUPNPC=1)
    message(STATUS "asteroids-net: UPnP automatic port mapping enabled")
else()
    message(STATUS "asteroids-net: miniupnpc not found; UPnP automatic port mapping disabled")
endif()

find_path(NATPMP_INCLUDE_DIR natpmp.h)
find_library(NATPMP_LIBRARY NAMES natpmp)
if(NATPMP_INCLUDE_DIR AND NATPMP_LIBRARY)
    target_include_directories(asteroids-net PRIVATE ${NATPMP_INCLUDE_DIR})
    target_link_libraries(asteroids-net PRIVATE ${NATPMP_LIBRARY})
    target_compile_definitions(asteroids-net PRIVATE ASTEROIDS_NET_HAS_NATPMP=1)
    message(STATUS "asteroids-net: NAT-PMP automatic port mapping enabled")
else()
    message(STATUS "asteroids-net: libnatpmp not found; NAT-PMP automatic port mapping disabled")
endif()

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
set(ASTEROIDS3D_FADE_OVERLAY_FRAGMENT_SHADER ${ASTEROIDS3D_SHADER_SOURCE_DIR}/fade_overlay.frag)
set(ASTEROIDS3D_FLAME_VERTEX_SHADER ${ASTEROIDS3D_SHADER_SOURCE_DIR}/flame.vert)
set(ASTEROIDS3D_FLAME_FRAGMENT_SHADER ${ASTEROIDS3D_SHADER_SOURCE_DIR}/flame.frag)
set(ASTEROIDS3D_MODEL_VERTEX_SPV ${ASTEROIDS3D_SHADER_OUTPUT_DIR}/model.vert.spv)
set(ASTEROIDS3D_MODEL_FRAGMENT_SPV ${ASTEROIDS3D_SHADER_OUTPUT_DIR}/model.frag.spv)
set(ASTEROIDS3D_INTRO_FRAGMENT_SPV ${ASTEROIDS3D_SHADER_OUTPUT_DIR}/intro.frag.spv)
set(ASTEROIDS3D_CRT_FRAGMENT_SPV ${ASTEROIDS3D_SHADER_OUTPUT_DIR}/crt.frag.spv)
set(ASTEROIDS3D_FADE_OVERLAY_FRAGMENT_SPV ${ASTEROIDS3D_SHADER_OUTPUT_DIR}/fade_overlay.frag.spv)
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
    OUTPUT ${ASTEROIDS3D_FADE_OVERLAY_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${ASTEROIDS3D_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${ASTEROIDS3D_FADE_OVERLAY_FRAGMENT_SHADER} -o ${ASTEROIDS3D_FADE_OVERLAY_FRAGMENT_SPV}
    DEPENDS ${ASTEROIDS3D_FADE_OVERLAY_FRAGMENT_SHADER}
    COMMENT "Compiling asteroids3d fade overlay fragment shader"
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

add_custom_target(asteroids-net_shaders
    DEPENDS
        ${ASTEROIDS3D_MODEL_VERTEX_SPV}
        ${ASTEROIDS3D_MODEL_FRAGMENT_SPV}
        ${ASTEROIDS3D_INTRO_FRAGMENT_SPV}
        ${ASTEROIDS3D_CRT_FRAGMENT_SPV}
        ${ASTEROIDS3D_FADE_OVERLAY_FRAGMENT_SPV}
        ${ASTEROIDS3D_FLAME_VERTEX_SPV}
        ${ASTEROIDS3D_FLAME_FRAGMENT_SPV}
)

add_dependencies(asteroids-net asteroids-net_shaders)
target_compile_definitions(asteroids-net PRIVATE
    ASTEROIDS3D_ASSET_DIR="$<TARGET_FILE_DIR:asteroids-net>"
    ASTEROIDS3D_SOURCE_DATA_DIR="$<TARGET_FILE_DIR:asteroids-net>/data"
    ASTEROIDS3D_DEFENDER_SOUND_DIR="$<TARGET_FILE_DIR:asteroids-net>/data"
)

add_custom_command(TARGET asteroids-net POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:asteroids-net>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:asteroids-net>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/rain/data/NotoSansCJK-Bold.ttc
        $<TARGET_FILE_DIR:asteroids-net>/data/NotoSansCJK-Bold.ttc
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/rain/data/NotoSansCJK-Regular.ttc
        $<TARGET_FILE_DIR:asteroids-net>/data/NotoSansCJK-Regular.ttc
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/music.ogg
        $<TARGET_FILE_DIR:asteroids-net>/data/music.ogg
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/cannon.wav
        $<TARGET_FILE_DIR:asteroids-net>/data/cannon.wav
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/defender/data/crash.wav
        ${CMAKE_SOURCE_DIR}/examples/defender/data/asteroid.wav
        $<TARGET_FILE_DIR:asteroids-net>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/asteroid.obj
        $<TARGET_FILE_DIR:asteroids-net>/data/asteroid.obj
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/asteroid2.obj
        $<TARGET_FILE_DIR:asteroids-net>/data/asteroid2.obj
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/asteroid3.obj
        $<TARGET_FILE_DIR:asteroids-net>/data/asteroid3.obj
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ASTEROIDS3D_MODEL_VERTEX_SPV}
        ${ASTEROIDS3D_MODEL_FRAGMENT_SPV}
        ${ASTEROIDS3D_INTRO_FRAGMENT_SPV}
        ${ASTEROIDS3D_CRT_FRAGMENT_SPV}
        ${ASTEROIDS3D_FADE_OVERLAY_FRAGMENT_SPV}
        ${ASTEROIDS3D_FLAME_VERTEX_SPV}
        ${ASTEROIDS3D_FLAME_FRAGMENT_SPV}
        $<TARGET_FILE_DIR:asteroids-net>/data
    COMMENT "Copying asteroids-net assets to runtime output directory"
)
