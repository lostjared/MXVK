include(GNUInstallDirs)

add_executable(mutatris
    ${CMAKE_SOURCE_DIR}/examples/mutatris/game_grid.cpp
    ${CMAKE_SOURCE_DIR}/examples/mutatris/mutatris.cpp
    ${CMAKE_SOURCE_DIR}/examples/mutatris/mutatris_window.cpp
    ${CMAKE_SOURCE_DIR}/examples/mutatris/piece.cpp
    ${CMAKE_SOURCE_DIR}/examples/mutatris/puzzle_game.cpp
)

target_link_libraries(mutatris PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(MUTATRIS_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(MUTATRIS_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(MUTATRIS_EFFECT_SHADER_SOURCE_DIR ${MUTATRIS_SHADER_SOURCE_DIR}/effects)
set(MUTATRIS_EFFECT_SHADER_OUTPUT_DIR ${MUTATRIS_SHADER_OUTPUT_DIR}/effects)
set(MUTATRIS_BACKGROUND_VERTEX_SHADER ${CMAKE_SOURCE_DIR}/mxvk/shaders/sprite.vert)
set(MUTATRIS_BACKGROUND_FRAGMENT_SHADER ${MUTATRIS_SHADER_SOURCE_DIR}/background.frag)
set(MUTATRIS_FADE_FRAGMENT_SHADER ${MUTATRIS_SHADER_SOURCE_DIR}/fade.frag)
set(MUTATRIS_CRT_FRAGMENT_SHADER ${MUTATRIS_SHADER_SOURCE_DIR}/crt.frag)
set(MUTATRIS_BACKGROUND_VERTEX_SPV ${MUTATRIS_SHADER_OUTPUT_DIR}/background.vert.spv)
set(MUTATRIS_BACKGROUND_FRAGMENT_SPV ${MUTATRIS_SHADER_OUTPUT_DIR}/background.frag.spv)
set(MUTATRIS_FADE_FRAGMENT_SPV ${MUTATRIS_SHADER_OUTPUT_DIR}/fade.frag.spv)
set(MUTATRIS_CRT_FRAGMENT_SPV ${MUTATRIS_SHADER_OUTPUT_DIR}/crt.frag.spv)
file(GLOB MUTATRIS_EFFECT_FRAGMENT_SHADERS CONFIGURE_DEPENDS ${MUTATRIS_EFFECT_SHADER_SOURCE_DIR}/*.glsl)

set(MUTATRIS_EFFECT_FRAGMENT_SPV)
foreach(EFFECT_SHADER ${MUTATRIS_EFFECT_FRAGMENT_SHADERS})
    get_filename_component(EFFECT_SHADER_NAME ${EFFECT_SHADER} NAME)
    set(EFFECT_SHADER_SPV ${MUTATRIS_EFFECT_SHADER_OUTPUT_DIR}/${EFFECT_SHADER_NAME}.spv)
    list(APPEND MUTATRIS_EFFECT_FRAGMENT_SPV ${EFFECT_SHADER_SPV})
    add_custom_command(
        OUTPUT ${EFFECT_SHADER_SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${MUTATRIS_EFFECT_SHADER_OUTPUT_DIR}
        COMMAND ${GLSLC_EXECUTABLE} -fshader-stage=frag ${EFFECT_SHADER} -o ${EFFECT_SHADER_SPV}
        DEPENDS ${EFFECT_SHADER}
        COMMENT "Compiling mutatris effect shader ${EFFECT_SHADER_NAME}"
        VERBATIM
    )
endforeach()

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

add_custom_command(
    OUTPUT ${MUTATRIS_CRT_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MUTATRIS_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${MUTATRIS_CRT_FRAGMENT_SHADER} -o ${MUTATRIS_CRT_FRAGMENT_SPV}
    DEPENDS ${MUTATRIS_CRT_FRAGMENT_SHADER}
    COMMENT "Compiling mutatris CRT fragment shader"
    VERBATIM
)

add_custom_target(mutatris_shaders
    DEPENDS
        ${MUTATRIS_BACKGROUND_VERTEX_SPV}
        ${MUTATRIS_BACKGROUND_FRAGMENT_SPV}
        ${MUTATRIS_FADE_FRAGMENT_SPV}
        ${MUTATRIS_CRT_FRAGMENT_SPV}
        ${MUTATRIS_EFFECT_FRAGMENT_SPV}
)

add_dependencies(mutatris mutatris_shaders)

target_compile_definitions(mutatris PRIVATE
    mutatris_ASSET_DIR="$<TARGET_FILE_DIR:mutatris>"
)

add_custom_command(TARGET mutatris POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:mutatris>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:mutatris>/data
    COMMENT "Copying mutatris assets to runtime output directory"
)

if(UNIX AND NOT APPLE)
    if(IS_ABSOLUTE "${CMAKE_INSTALL_BINDIR}")
        set(MUTATRIS_SCRIPT_TO_PREFIX ".")
        set(MUTATRIS_BINARY_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/mutatris/mutatris")
        set(MUTATRIS_ASSET_PATH "${CMAKE_INSTALL_FULL_DATADIR}/mutatris")
    else()
        file(RELATIVE_PATH MUTATRIS_SCRIPT_TO_PREFIX "/${CMAKE_INSTALL_BINDIR}" "/")
        if(MUTATRIS_SCRIPT_TO_PREFIX STREQUAL "")
            set(MUTATRIS_SCRIPT_TO_PREFIX ".")
        endif()

        if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBEXECDIR}")
            set(MUTATRIS_BINARY_PATH "${CMAKE_INSTALL_LIBEXECDIR}/mutatris/mutatris")
        else()
            set(MUTATRIS_BINARY_PATH "\${prefix}/${CMAKE_INSTALL_LIBEXECDIR}/mutatris/mutatris")
        endif()

        if(IS_ABSOLUTE "${CMAKE_INSTALL_DATADIR}")
            set(MUTATRIS_ASSET_PATH "${CMAKE_INSTALL_DATADIR}/mutatris")
        else()
            set(MUTATRIS_ASSET_PATH "\${prefix}/${CMAKE_INSTALL_DATADIR}/mutatris")
        endif()
    endif()

    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/mutatris.in
        ${CMAKE_CURRENT_BINARY_DIR}/mutatris-launcher
        @ONLY
    )

    set(MUTATRIS_INSTALL_DATADIR ${CMAKE_INSTALL_DATADIR}/mutatris)

    install(TARGETS mutatris
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/mutatris
    )

    install(PROGRAMS
        ${CMAKE_CURRENT_BINARY_DIR}/mutatris-launcher
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME mutatris
    )

    install(DIRECTORY
        ${CMAKE_CURRENT_SOURCE_DIR}/data/
        DESTINATION ${MUTATRIS_INSTALL_DATADIR}/data
    )

    install(FILES
        ${MUTATRIS_BACKGROUND_VERTEX_SPV}
        ${MUTATRIS_BACKGROUND_FRAGMENT_SPV}
        ${MUTATRIS_FADE_FRAGMENT_SPV}
        ${MUTATRIS_CRT_FRAGMENT_SPV}
        DESTINATION ${MUTATRIS_INSTALL_DATADIR}/shaders
    )

    if(MUTATRIS_EFFECT_FRAGMENT_SPV)
        install(FILES
            ${MUTATRIS_EFFECT_FRAGMENT_SPV}
            DESTINATION ${MUTATRIS_INSTALL_DATADIR}/shaders/effects
        )
    endif()

    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/mutatris.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )

    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/data/icon.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/64x64/apps
        RENAME mutatris.png
    )
endif()
