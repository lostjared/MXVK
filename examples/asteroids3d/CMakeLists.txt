include(GNUInstallDirs)

option(ASTEROIDS3D_INSTALL_USER_DESKTOP "Install asteroids3d desktop entry and icon to the user's local data directory" ON)

add_executable(asteroids3d
    ${CMAKE_SOURCE_DIR}/examples/asteroids3d/main.cpp
    ${CMAKE_SOURCE_DIR}/examples/asteroids3d/asteroids3d_window.cpp
    ${CMAKE_SOURCE_DIR}/examples/asteroids3d/ship.cpp
    ${CMAKE_SOURCE_DIR}/examples/asteroids3d/starfield.cpp
)
if(MINGW)
    target_sources(asteroids3d PRIVATE data/icon.rc)
endif()
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

add_custom_target(asteroids3d_shaders
    DEPENDS
        ${ASTEROIDS3D_MODEL_VERTEX_SPV}
        ${ASTEROIDS3D_MODEL_FRAGMENT_SPV}
        ${ASTEROIDS3D_INTRO_FRAGMENT_SPV}
        ${ASTEROIDS3D_CRT_FRAGMENT_SPV}
        ${ASTEROIDS3D_FADE_OVERLAY_FRAGMENT_SPV}
        ${ASTEROIDS3D_FLAME_VERTEX_SPV}
        ${ASTEROIDS3D_FLAME_FRAGMENT_SPV}
)

add_dependencies(asteroids3d asteroids3d_shaders)
target_compile_definitions(asteroids3d PRIVATE
    ASTEROIDS3D_ASSET_DIR="$<TARGET_FILE_DIR:asteroids3d>"
    ASTEROIDS3D_SOURCE_DATA_DIR="$<TARGET_FILE_DIR:asteroids3d>/data"
    ASTEROIDS3D_DEFENDER_SOUND_DIR="$<TARGET_FILE_DIR:asteroids3d>/data"
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
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ASTEROIDS3D_MODEL_VERTEX_SPV}
        ${ASTEROIDS3D_MODEL_FRAGMENT_SPV}
        ${ASTEROIDS3D_INTRO_FRAGMENT_SPV}
        ${ASTEROIDS3D_CRT_FRAGMENT_SPV}
        ${ASTEROIDS3D_FADE_OVERLAY_FRAGMENT_SPV}
        ${ASTEROIDS3D_FLAME_VERTEX_SPV}
        ${ASTEROIDS3D_FLAME_FRAGMENT_SPV}
        $<TARGET_FILE_DIR:asteroids3d>/data
    COMMENT "Copying asteroids3d assets to runtime output directory"
)

if(UNIX AND NOT APPLE)
    if(IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}")
        set(ASTEROIDS3D_SCRIPT_TO_PREFIX ".")
        set(ASTEROIDS3D_BINARY_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/asteroids3d/asteroids3d")
        set(ASTEROIDS3D_ASSET_PATH "${CMAKE_INSTALL_FULL_DATADIR}/asteroids3d")
    else()
        file(RELATIVE_PATH ASTEROIDS3D_SCRIPT_TO_PREFIX "/${CMAKE_INSTALL_BINDIR}" "/")
        if(ASTEROIDS3D_SCRIPT_TO_PREFIX STREQUAL "")
            set(ASTEROIDS3D_SCRIPT_TO_PREFIX ".")
        endif()

        if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBEXECDIR}")
            set(ASTEROIDS3D_BINARY_PATH "${CMAKE_INSTALL_LIBEXECDIR}/asteroids3d/asteroids3d")
        else()
            set(ASTEROIDS3D_BINARY_PATH "\${prefix}/${CMAKE_INSTALL_LIBEXECDIR}/asteroids3d/asteroids3d")
        endif()

        if(IS_ABSOLUTE "${CMAKE_INSTALL_DATADIR}")
            set(ASTEROIDS3D_ASSET_PATH "${CMAKE_INSTALL_DATADIR}/asteroids3d")
        else()
            set(ASTEROIDS3D_ASSET_PATH "\${prefix}/${CMAKE_INSTALL_DATADIR}/asteroids3d")
        endif()
    endif()

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/asteroids3d.in
        ${CMAKE_CURRENT_BINARY_DIR}/asteroids3d-launcher
        @ONLY
    )

    set(ASTEROIDS3D_INSTALL_DATADIR ${CMAKE_INSTALL_DATADIR}/asteroids3d)

    install(TARGETS asteroids3d
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/asteroids3d
    )
    install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/asteroids3d-launcher
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME asteroids3d
    )
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/
        DESTINATION ${ASTEROIDS3D_INSTALL_DATADIR}/data
    )
    install(FILES
        ${CMAKE_SOURCE_DIR}/examples/rain/data/NotoSansCJK-Bold.ttc
        ${CMAKE_SOURCE_DIR}/examples/rain/data/NotoSansCJK-Regular.ttc
        ${CMAKE_SOURCE_DIR}/examples/defender/data/crash.wav
        ${CMAKE_SOURCE_DIR}/examples/defender/data/asteroid.wav
        ${ASTEROIDS3D_MODEL_VERTEX_SPV}
        ${ASTEROIDS3D_MODEL_FRAGMENT_SPV}
        ${ASTEROIDS3D_INTRO_FRAGMENT_SPV}
        ${ASTEROIDS3D_CRT_FRAGMENT_SPV}
        ${ASTEROIDS3D_FADE_OVERLAY_FRAGMENT_SPV}
        ${ASTEROIDS3D_FLAME_VERTEX_SPV}
        ${ASTEROIDS3D_FLAME_FRAGMENT_SPV}
        DESTINATION ${ASTEROIDS3D_INSTALL_DATADIR}/data
    )

    if(ASTEROIDS3D_INSTALL_USER_DESKTOP)
        install(CODE "
            set(ASTEROIDS3D_DESKTOP_INSTALL_HOME \"\$ENV{HOME}\")
            set(ASTEROIDS3D_DESKTOP_INSTALL_OWNER)
            if(DEFINED ENV{SUDO_USER} AND DEFINED ENV{SUDO_UID} AND DEFINED ENV{SUDO_GID})
                execute_process(COMMAND getent passwd \"\$ENV{SUDO_USER}\" OUTPUT_VARIABLE ASTEROIDS3D_SUDO_PASSWD OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
                if(ASTEROIDS3D_SUDO_PASSWD)
                    string(REPLACE \":\" \";\" ASTEROIDS3D_SUDO_PASSWD_FIELDS \"\${ASTEROIDS3D_SUDO_PASSWD}\")
                    list(GET ASTEROIDS3D_SUDO_PASSWD_FIELDS 5 ASTEROIDS3D_DESKTOP_INSTALL_HOME)
                endif()
                set(ASTEROIDS3D_DESKTOP_INSTALL_OWNER \"\$ENV{SUDO_UID}:\$ENV{SUDO_GID}\")
            endif()

            if(NOT ASTEROIDS3D_DESKTOP_INSTALL_HOME)
                message(WARNING \"Skipping asteroids3d desktop install because HOME is not set\")
            else()
                set(ASTEROIDS3D_DESKTOP_DIR \"\${ASTEROIDS3D_DESKTOP_INSTALL_HOME}/.local/share/applications\")
                set(ASTEROIDS3D_ICON_DIR \"\${ASTEROIDS3D_DESKTOP_INSTALL_HOME}/.local/share/icons/hicolor/64x64/apps\")
                file(INSTALL DESTINATION \"\${ASTEROIDS3D_DESKTOP_DIR}\" TYPE FILE FILES \"${CMAKE_CURRENT_SOURCE_DIR}/asteroids3d.desktop\")
                file(INSTALL DESTINATION \"\${ASTEROIDS3D_ICON_DIR}\" TYPE FILE RENAME \"asteroids3d.png\" FILES \"${CMAKE_CURRENT_SOURCE_DIR}/data/asteroids_icon.png\")
                if(ASTEROIDS3D_DESKTOP_INSTALL_OWNER)
                    execute_process(COMMAND chown \"\${ASTEROIDS3D_DESKTOP_INSTALL_OWNER}\" \"\${ASTEROIDS3D_DESKTOP_DIR}/asteroids3d.desktop\" \"\${ASTEROIDS3D_ICON_DIR}/asteroids3d.png\")
                endif()
            endif()
        ")
    endif()
endif()
