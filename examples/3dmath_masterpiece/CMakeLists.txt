add_executable(math3d_masterpiece
    ${CMAKE_SOURCE_DIR}/examples/3dmath_masterpiece/main.cpp
)

target_link_libraries(math3d_masterpiece PRIVATE mxvk::mxvk)
mxvk_configure_3dmath_backend(math3d_masterpiece)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set_target_properties(math3d_masterpiece PROPERTIES
    OUTPUT_NAME "3dmath_masterpiece"
)

set(math3d_masterpiece_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(math3d_masterpiece_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(math3d_masterpiece_RUNTIME_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)
set(math3d_masterpiece_INTRO_FRAGMENT_SHADER ${math3d_masterpiece_SHADER_SOURCE_DIR}/intro.frag)
set(math3d_masterpiece_INTRO_FRAGMENT_SPV ${math3d_masterpiece_SHADER_OUTPUT_DIR}/intro.frag.spv)
file(GLOB math3d_masterpiece_RUNTIME_DATA_FILES
    ${math3d_masterpiece_SHADER_SOURCE_DIR}/*
)

add_custom_command(
    OUTPUT ${math3d_masterpiece_INTRO_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${math3d_masterpiece_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${math3d_masterpiece_INTRO_FRAGMENT_SHADER} -o ${math3d_masterpiece_INTRO_FRAGMENT_SPV}
    DEPENDS ${math3d_masterpiece_INTRO_FRAGMENT_SHADER}
    COMMENT "Compiling 3D Math MasterPiece intro fragment shader"
    VERBATIM
)

add_custom_target(math3d_masterpiece_shaders
    DEPENDS ${math3d_masterpiece_INTRO_FRAGMENT_SPV}
)

add_dependencies(math3d_masterpiece math3d_masterpiece_shaders)
if(TARGET mxvk_shaders)
    add_dependencies(math3d_masterpiece mxvk_shaders)
endif()

target_compile_definitions(math3d_masterpiece PRIVATE
    MASTERPIECE_ASSET_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

add_custom_target(math3d_masterpiece_runtime_data
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${math3d_masterpiece_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${math3d_masterpiece_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        ${math3d_masterpiece_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/data/mxvk_logo.png
        ${math3d_masterpiece_RUNTIME_DATA_DIR}/mxvk_logo.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mxvk_VERTEX_SPV}
        ${math3d_masterpiece_RUNTIME_DATA_DIR}/sprite.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mxvk_VERTEX_SPV}
        ${math3d_masterpiece_RUNTIME_DATA_DIR}/sprite_vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mxvk_FRAGMENT_SPV}
        ${math3d_masterpiece_RUNTIME_DATA_DIR}/sprite.frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mxvk_FRAGMENT_SPV}
        ${math3d_masterpiece_RUNTIME_DATA_DIR}/sprite_frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mxvk_TEXT_VERTEX_SPV}
        ${math3d_masterpiece_RUNTIME_DATA_DIR}/text.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mxvk_TEXT_VERTEX_SPV}
        ${math3d_masterpiece_RUNTIME_DATA_DIR}/text_vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mxvk_TEXT_FRAGMENT_SPV}
        ${math3d_masterpiece_RUNTIME_DATA_DIR}/text.frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${mxvk_TEXT_FRAGMENT_SPV}
        ${math3d_masterpiece_RUNTIME_DATA_DIR}/text_frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${math3d_masterpiece_INTRO_FRAGMENT_SPV}
        ${math3d_masterpiece_RUNTIME_DATA_DIR}/intro.frag.spv
    DEPENDS
        ${math3d_masterpiece_RUNTIME_DATA_FILES}
        ${math3d_masterpiece_INTRO_FRAGMENT_SPV}
    COMMENT "Copying 3D Math MasterPiece runtime data"
    VERBATIM
)

add_dependencies(math3d_masterpiece_runtime_data math3d_masterpiece_shaders)
if(TARGET mxvk_shaders)
    add_dependencies(math3d_masterpiece_runtime_data mxvk_shaders)
endif()

add_dependencies(math3d_masterpiece math3d_masterpiece_runtime_data)
