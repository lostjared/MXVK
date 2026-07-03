add_executable(pointsprite
    ${CMAKE_CURRENT_SOURCE_DIR}/point.cpp
)
target_link_libraries(pointsprite PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(POINTSPRITE_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(POINTSPRITE_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(POINTSPRITE_RUNTIME_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)

set(POINTSPRITE_VERTEX_SHADER ${POINTSPRITE_SHADER_SOURCE_DIR}/pointsprite.vert)
set(POINTSPRITE_FRAGMENT_SHADER ${POINTSPRITE_SHADER_SOURCE_DIR}/pointsprite.frag)
set(POINTSPRITE_VERTEX_SPV ${POINTSPRITE_SHADER_OUTPUT_DIR}/pointsprite.vert.spv)
set(POINTSPRITE_FRAGMENT_SPV ${POINTSPRITE_SHADER_OUTPUT_DIR}/pointsprite.frag.spv)

add_custom_command(
    OUTPUT ${POINTSPRITE_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${POINTSPRITE_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${POINTSPRITE_VERTEX_SHADER} -o ${POINTSPRITE_VERTEX_SPV}
    DEPENDS ${POINTSPRITE_VERTEX_SHADER}
    COMMENT "Compiling pointsprite vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${POINTSPRITE_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${POINTSPRITE_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${POINTSPRITE_FRAGMENT_SHADER} -o ${POINTSPRITE_FRAGMENT_SPV}
    DEPENDS ${POINTSPRITE_FRAGMENT_SHADER}
    COMMENT "Compiling pointsprite fragment shader"
    VERBATIM
)

add_custom_target(pointsprite_shaders
    COMMAND ${CMAKE_COMMAND} -E make_directory ${POINTSPRITE_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${POINTSPRITE_VERTEX_SPV}
        ${POINTSPRITE_RUNTIME_DATA_DIR}/pointsprite.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${POINTSPRITE_FRAGMENT_SPV}
        ${POINTSPRITE_RUNTIME_DATA_DIR}/pointsprite.frag.spv
    DEPENDS
        ${POINTSPRITE_VERTEX_SPV}
        ${POINTSPRITE_FRAGMENT_SPV}
    COMMENT "Copying pointsprite SPIR-V shaders to runtime data directory"
    VERBATIM
)

add_custom_target(pointsprite_runtime_data
    COMMAND ${CMAKE_COMMAND} -E make_directory ${POINTSPRITE_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        ${POINTSPRITE_RUNTIME_DATA_DIR}
    COMMENT "Copying pointsprite runtime assets to build data directory"
    VERBATIM
)

add_dependencies(pointsprite pointsprite_shaders pointsprite_runtime_data)
target_compile_definitions(pointsprite PRIVATE
    POINTSPRITE_ASSET_DIR="$<TARGET_FILE_DIR:pointsprite>"
)

add_custom_command(TARGET pointsprite POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:pointsprite>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:pointsprite>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${POINTSPRITE_VERTEX_SPV}
        $<TARGET_FILE_DIR:pointsprite>/data/pointsprite.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${POINTSPRITE_FRAGMENT_SPV}
        $<TARGET_FILE_DIR:pointsprite>/data/pointsprite.frag.spv
    COMMENT "Copying pointsprite assets to runtime output directory"
)
