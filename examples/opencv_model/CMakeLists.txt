add_executable(opencv_model
    ${CMAKE_SOURCE_DIR}/examples/opencv_model/main.cpp
)
target_link_libraries(opencv_model PRIVATE mxvk::mxvk)
target_include_directories(opencv_model PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(opencv_model PRIVATE ${OpenCV_LIBS})

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
    opencv_model_ASSET_DIR="$<TARGET_FILE_DIR:opencv_model>"
)

set(opencv_model_DATA_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
add_custom_command(TARGET opencv_model POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:opencv_model>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${opencv_model_DATA_SOURCE_DIR}
        $<TARGET_FILE_DIR:opencv_model>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${opencv_model_VERTEX_SPV}
        ${opencv_model_FRAGMENT_SPV}
        $<TARGET_FILE_DIR:opencv_model>/data
    COMMENT "Copying opencv_model data directory to runtime output directory"
)
