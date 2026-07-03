add_executable(viewer
    ${CMAKE_SOURCE_DIR}/examples/viewer/viewer.cpp
)
target_link_libraries(viewer PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(VIEWER_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(VIEWER_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

set(VIEWER_VERTEX_SHADER ${VIEWER_SHADER_SOURCE_DIR}/model.vert)
set(VIEWER_FRAGMENT_SHADER ${VIEWER_SHADER_SOURCE_DIR}/model.frag)
set(VIEWER_VERTEX_SPV ${VIEWER_SHADER_OUTPUT_DIR}/model.vert.spv)
set(VIEWER_FRAGMENT_SPV ${VIEWER_SHADER_OUTPUT_DIR}/model.frag.spv)

add_custom_command(
    OUTPUT ${VIEWER_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VIEWER_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${VIEWER_VERTEX_SHADER} -o ${VIEWER_VERTEX_SPV}
    DEPENDS ${VIEWER_VERTEX_SHADER}
    COMMENT "Compiling viewer model vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${VIEWER_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${VIEWER_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${VIEWER_FRAGMENT_SHADER} -o ${VIEWER_FRAGMENT_SPV}
    DEPENDS ${VIEWER_FRAGMENT_SHADER}
    COMMENT "Compiling viewer model fragment shader"
    VERBATIM
)

add_custom_target(viewer_shaders
    DEPENDS
        ${VIEWER_VERTEX_SPV}
        ${VIEWER_FRAGMENT_SPV}
)

add_dependencies(viewer viewer_shaders)
target_compile_definitions(viewer PRIVATE
    VIEWER_ASSET_DIR="$<TARGET_FILE_DIR:viewer>"
    VIEWER_SHADER_DIR="${VIEWER_SHADER_OUTPUT_DIR}"
    VIEWER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
