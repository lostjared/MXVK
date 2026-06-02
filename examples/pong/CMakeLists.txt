add_executable(pong
    ${CMAKE_SOURCE_DIR}/examples/pong/pong.cpp
)

target_link_libraries(pong PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(PONG_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(PONG_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(PONG_VERTEX_SHADER ${PONG_SHADER_SOURCE_DIR}/model.vert)
set(PONG_FRAGMENT_SHADER ${PONG_SHADER_SOURCE_DIR}/model.frag)
set(PONG_VERTEX_SPV ${PONG_SHADER_OUTPUT_DIR}/pong_model.vert.spv)
set(PONG_FRAGMENT_SPV ${PONG_SHADER_OUTPUT_DIR}/pong_model.frag.spv)

add_custom_command(
    OUTPUT ${PONG_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PONG_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${PONG_VERTEX_SHADER} -o ${PONG_VERTEX_SPV}
    DEPENDS ${PONG_VERTEX_SHADER}
    COMMENT "Compiling pong vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${PONG_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PONG_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${PONG_FRAGMENT_SHADER} -o ${PONG_FRAGMENT_SPV}
    DEPENDS ${PONG_FRAGMENT_SHADER}
    COMMENT "Compiling pong fragment shader"
    VERBATIM
)

add_custom_target(pong_shaders
    DEPENDS
        ${PONG_VERTEX_SPV}
        ${PONG_FRAGMENT_SPV}
)

add_dependencies(pong pong_shaders)

target_compile_definitions(pong PRIVATE
    pong_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    pong_SHADER_DIR="${PONG_SHADER_OUTPUT_DIR}"
)

add_custom_command(TARGET pong POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:pong>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:pong>/data
    COMMENT "Copying pong assets to runtime output directory"
)
