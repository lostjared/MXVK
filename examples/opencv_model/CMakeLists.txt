add_executable(opencv_model
    ${CMAKE_SOURCE_DIR}/examples/opencv_model/main.cpp
)
target_link_libraries(opencv_model PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(opencv_model_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(opencv_model_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

set(opencv_model_VERTEX_SHADER ${opencv_model_SHADER_SOURCE_DIR}/vertex.vert)
set(opencv_model_FRAGMENT_SHADER ${opencv_model_SHADER_SOURCE_DIR}/fragment.frag)
set(opencv_model_VERTEX_SPV ${opencv_model_SHADER_OUTPUT_DIR}/model.vert.spv)
set(opencv_model_FRAGMENT_SPV ${opencv_model_SHADER_OUTPUT_DIR}/model.frag.spv)

add_custom_command(
    OUTPUT ${opencv_model_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${opencv_model_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${opencv_model_VERTEX_SHADER} -o ${opencv_model_VERTEX_SPV}
    DEPENDS ${opencv_model_VERTEX_SHADER}
    COMMENT "Compiling triangle vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${opencv_model_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${opencv_model_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${opencv_model_FRAGMENT_SHADER} -o ${opencv_model_FRAGMENT_SPV}
    DEPENDS ${opencv_model_FRAGMENT_SHADER}
    COMMENT "Compiling triangle fragment shader"
    VERBATIM
)

add_custom_target(opencv_model_shaders
    DEPENDS
        ${opencv_model_VERTEX_SPV}
        ${opencv_model_FRAGMENT_SPV}
)

add_dependencies(opencv_model opencv_model_shaders)
target_compile_definitions(opencv_model PRIVATE
    opencv_model_SHADER_DIR="${opencv_model_SHADER_OUTPUT_DIR}"
    opencv_model_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
