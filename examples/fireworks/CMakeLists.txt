add_executable(fireworks
    ${CMAKE_CURRENT_SOURCE_DIR}/fireworks.cpp
)
target_link_libraries(fireworks PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(FIREWORKS_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(FIREWORKS_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(FIREWORKS_RUNTIME_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)

set(FIREWORKS_VERTEX_SHADER ${FIREWORKS_SHADER_SOURCE_DIR}/fireworks.vert)
set(FIREWORKS_FRAGMENT_SHADER ${FIREWORKS_SHADER_SOURCE_DIR}/fireworks.frag)
set(FIREWORKS_VERTEX_SPV ${FIREWORKS_SHADER_OUTPUT_DIR}/fireworks.vert.spv)
set(FIREWORKS_FRAGMENT_SPV ${FIREWORKS_SHADER_OUTPUT_DIR}/fireworks.frag.spv)

add_custom_command(
    OUTPUT ${FIREWORKS_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${FIREWORKS_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${FIREWORKS_VERTEX_SHADER} -o ${FIREWORKS_VERTEX_SPV}
    DEPENDS ${FIREWORKS_VERTEX_SHADER}
    COMMENT "Compiling fireworks vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${FIREWORKS_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${FIREWORKS_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${FIREWORKS_FRAGMENT_SHADER} -o ${FIREWORKS_FRAGMENT_SPV}
    DEPENDS ${FIREWORKS_FRAGMENT_SHADER}
    COMMENT "Compiling fireworks fragment shader"
    VERBATIM
)

add_custom_target(fireworks_shaders
    COMMAND ${CMAKE_COMMAND} -E make_directory ${FIREWORKS_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${FIREWORKS_VERTEX_SPV}
        ${FIREWORKS_RUNTIME_DATA_DIR}/fireworks.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${FIREWORKS_FRAGMENT_SPV}
        ${FIREWORKS_RUNTIME_DATA_DIR}/fireworks.frag.spv
    DEPENDS
        ${FIREWORKS_VERTEX_SPV}
        ${FIREWORKS_FRAGMENT_SPV}
    COMMENT "Copying fireworks SPIR-V shaders to runtime data directory"
    VERBATIM
)

add_custom_target(fireworks_runtime_data
    COMMAND ${CMAKE_COMMAND} -E make_directory ${FIREWORKS_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        ${FIREWORKS_RUNTIME_DATA_DIR}
    COMMENT "Copying fireworks runtime assets to build data directory"
    VERBATIM
)

add_dependencies(fireworks fireworks_shaders fireworks_runtime_data)
target_compile_definitions(fireworks PRIVATE
    FIREWORKS_ASSET_DIR="$<TARGET_FILE_DIR:fireworks>"
)

add_custom_command(TARGET fireworks POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:fireworks>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:fireworks>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${FIREWORKS_VERTEX_SPV}
        $<TARGET_FILE_DIR:fireworks>/data/fireworks.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${FIREWORKS_FRAGMENT_SPV}
        $<TARGET_FILE_DIR:fireworks>/data/fireworks.frag.spv
    COMMENT "Copying fireworks assets to runtime output directory"
)
