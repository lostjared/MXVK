add_executable(bluesky
    ${CMAKE_SOURCE_DIR}/examples/bluesky/bluesky.cpp
)
target_link_libraries(bluesky PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(WATER_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(WATER_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(WATER_RUNTIME_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)

set(WATER_VERTEX_SHADER ${WATER_SHADER_SOURCE_DIR}/water.vert)
set(WATER_FRAGMENT_SHADER ${WATER_SHADER_SOURCE_DIR}/water.frag)
set(WATER_SKY_VERTEX_SHADER ${WATER_SHADER_SOURCE_DIR}/sky.vert)
set(WATER_SKY_FRAGMENT_SHADER ${WATER_SHADER_SOURCE_DIR}/sky.frag)
set(WATER_VERTEX_SPV ${WATER_SHADER_OUTPUT_DIR}/water.vert.spv)
set(WATER_FRAGMENT_SPV ${WATER_SHADER_OUTPUT_DIR}/water.frag.spv)
set(WATER_SKY_VERTEX_SPV ${WATER_SHADER_OUTPUT_DIR}/sky.vert.spv)
set(WATER_SKY_FRAGMENT_SPV ${WATER_SHADER_OUTPUT_DIR}/sky.frag.spv)

add_custom_command(
    OUTPUT ${WATER_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${WATER_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${WATER_VERTEX_SHADER} -o ${WATER_VERTEX_SPV}
    DEPENDS ${WATER_VERTEX_SHADER}
    COMMENT "Compiling water vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${WATER_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${WATER_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${WATER_FRAGMENT_SHADER} -o ${WATER_FRAGMENT_SPV}
    DEPENDS ${WATER_FRAGMENT_SHADER}
    COMMENT "Compiling water fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${WATER_SKY_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${WATER_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${WATER_SKY_VERTEX_SHADER} -o ${WATER_SKY_VERTEX_SPV}
    DEPENDS ${WATER_SKY_VERTEX_SHADER}
    COMMENT "Compiling water sky vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${WATER_SKY_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${WATER_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${WATER_SKY_FRAGMENT_SHADER} -o ${WATER_SKY_FRAGMENT_SPV}
    DEPENDS ${WATER_SKY_FRAGMENT_SHADER}
    COMMENT "Compiling water sky fragment shader"
    VERBATIM
)

add_custom_target(bluesky_shaders
    COMMAND ${CMAKE_COMMAND} -E make_directory ${WATER_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${WATER_VERTEX_SPV}
        ${WATER_RUNTIME_DATA_DIR}/water.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${WATER_FRAGMENT_SPV}
        ${WATER_RUNTIME_DATA_DIR}/water.frag.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${WATER_SKY_VERTEX_SPV}
        ${WATER_RUNTIME_DATA_DIR}/sky.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${WATER_SKY_FRAGMENT_SPV}
        ${WATER_RUNTIME_DATA_DIR}/sky.frag.spv
    DEPENDS
        ${WATER_VERTEX_SPV}
        ${WATER_FRAGMENT_SPV}
        ${WATER_SKY_VERTEX_SPV}
        ${WATER_SKY_FRAGMENT_SPV}
    COMMENT "Copying water SPIR-V shaders to runtime data directory"
    VERBATIM
)

add_dependencies(bluesky bluesky_shaders)
target_compile_definitions(bluesky PRIVATE
    WATER_ASSET_DIR="$<TARGET_FILE_DIR:bluesky>"
)
