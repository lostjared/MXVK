add_executable(masterpiece
    ${CMAKE_SOURCE_DIR}/examples/masterpiece/main.cpp
)

target_link_libraries(masterpiece PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set_target_properties(masterpiece PROPERTIES
    OUTPUT_NAME "MasterPiece"
)

set(masterpiece_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(masterpiece_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(masterpiece_RUNTIME_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)
set(masterpiece_INTRO_FRAGMENT_SHADER ${masterpiece_SHADER_SOURCE_DIR}/intro.frag)
set(masterpiece_INTRO_FRAGMENT_SPV ${masterpiece_SHADER_OUTPUT_DIR}/intro.frag.spv)
file(GLOB masterpiece_RUNTIME_DATA_FILES
    ${masterpiece_SHADER_SOURCE_DIR}/*
)

add_custom_command(
    OUTPUT ${masterpiece_INTRO_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${masterpiece_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${masterpiece_INTRO_FRAGMENT_SHADER} -o ${masterpiece_INTRO_FRAGMENT_SPV}
    DEPENDS ${masterpiece_INTRO_FRAGMENT_SHADER}
    COMMENT "Compiling MasterPiece intro fragment shader"
    VERBATIM
)

add_custom_target(masterpiece_shaders
    DEPENDS ${masterpiece_INTRO_FRAGMENT_SPV}
)

add_dependencies(masterpiece masterpiece_shaders)
if(TARGET mxvk_shaders)
    add_dependencies(masterpiece mxvk_shaders)
endif()

target_compile_definitions(masterpiece PRIVATE
    MASTERPIECE_ASSET_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

add_custom_target(masterpiece_runtime_data
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${masterpiece_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${masterpiece_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        ${masterpiece_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/mxvk_logo.png
        ${masterpiece_RUNTIME_DATA_DIR}/mxvk_logo.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mxvk_VERTEX_SPV}
        ${masterpiece_RUNTIME_DATA_DIR}/sprite.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mxvk_VERTEX_SPV}
        ${masterpiece_RUNTIME_DATA_DIR}/sprite_vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mxvk_FRAGMENT_SPV}
        ${masterpiece_RUNTIME_DATA_DIR}/sprite.frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mxvk_FRAGMENT_SPV}
        ${masterpiece_RUNTIME_DATA_DIR}/sprite_frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mxvk_TEXT_VERTEX_SPV}
        ${masterpiece_RUNTIME_DATA_DIR}/text.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mxvk_TEXT_VERTEX_SPV}
        ${masterpiece_RUNTIME_DATA_DIR}/text_vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mxvk_TEXT_FRAGMENT_SPV}
        ${masterpiece_RUNTIME_DATA_DIR}/text.frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mxvk_TEXT_FRAGMENT_SPV}
        ${masterpiece_RUNTIME_DATA_DIR}/text_frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${masterpiece_INTRO_FRAGMENT_SPV}
        ${masterpiece_RUNTIME_DATA_DIR}/intro.frag.spv
    DEPENDS
        ${masterpiece_RUNTIME_DATA_FILES}
        ${masterpiece_INTRO_FRAGMENT_SPV}
    COMMENT "Copying MasterPiece runtime data"
    VERBATIM
)

add_dependencies(masterpiece_runtime_data masterpiece_shaders)
if(TARGET mxvk_shaders)
    add_dependencies(masterpiece_runtime_data mxvk_shaders)
endif()

add_dependencies(masterpiece masterpiece_runtime_data)
