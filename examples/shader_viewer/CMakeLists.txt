add_executable(shader_viewer
    ${CMAKE_SOURCE_DIR}/examples/shader_viewer/shaders.cpp
)
target_link_libraries(shader_viewer PRIVATE mxvk::mxvk)
target_include_directories(shader_viewer PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(shader_viewer PRIVATE ${OpenCV_LIBS})

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(shader_viewer_SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/examples/opencv_example/shaders)
set(shader_viewer_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

set(shader_viewer_VERTEX_SHADER ${shader_viewer_SHADER_SOURCE_DIR}/vertex.vert)
set(shader_viewer_FRAGMENT_SHADER ${shader_viewer_SHADER_SOURCE_DIR}/fragment.frag)
set(shader_viewer_VERTEX_SPV ${shader_viewer_SHADER_OUTPUT_DIR}/vertex.vert.spv)
set(shader_viewer_FRAGMENT_SPV ${shader_viewer_SHADER_OUTPUT_DIR}/fragment.frag.spv)

add_custom_command(
    OUTPUT ${shader_viewer_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_viewer_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${shader_viewer_VERTEX_SHADER} -o ${shader_viewer_VERTEX_SPV}
    DEPENDS ${shader_viewer_VERTEX_SHADER}
    COMMENT "Compiling shader viewer vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${shader_viewer_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_viewer_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${shader_viewer_FRAGMENT_SHADER} -o ${shader_viewer_FRAGMENT_SPV}
    DEPENDS ${shader_viewer_FRAGMENT_SHADER}
    COMMENT "Compiling shader viewer fragment shader"
    VERBATIM
)

add_custom_target(shader_viewer_shaders
    DEPENDS
        ${shader_viewer_VERTEX_SPV}
        ${shader_viewer_FRAGMENT_SPV}
)

add_dependencies(shader_viewer shader_viewer_shaders)
target_compile_definitions(shader_viewer PRIVATE
    shader_viewer_ASSET_DIR="$<TARGET_FILE_DIR:shader_viewer>"
)

add_custom_command(TARGET shader_viewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:shader_viewer>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/examples/opencv_example/data
        $<TARGET_FILE_DIR:shader_viewer>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${shader_viewer_VERTEX_SPV}
        ${shader_viewer_FRAGMENT_SPV}
        $<TARGET_FILE_DIR:shader_viewer>/data
    COMMENT "Copying shader_viewer data directory to runtime output directory"
)
