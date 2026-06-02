add_executable(model_example
    ${CMAKE_SOURCE_DIR}/examples/model_example/main.cpp
)
target_link_libraries(model_example PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(MODEL_EXAMPLE_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(MODEL_EXAMPLE_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)

set(MODEL_EXAMPLE_VERTEX_SHADER ${MODEL_EXAMPLE_SHADER_SOURCE_DIR}/model.vert)
set(MODEL_EXAMPLE_FRAGMENT_SHADER ${MODEL_EXAMPLE_SHADER_SOURCE_DIR}/model.frag)
set(MODEL_EXAMPLE_VERTEX_SPV ${MODEL_EXAMPLE_SHADER_OUTPUT_DIR}/model.vert.spv)
set(MODEL_EXAMPLE_FRAGMENT_SPV ${MODEL_EXAMPLE_SHADER_OUTPUT_DIR}/model.frag.spv)

add_custom_command(
    OUTPUT ${MODEL_EXAMPLE_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MODEL_EXAMPLE_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${MODEL_EXAMPLE_VERTEX_SHADER} -o ${MODEL_EXAMPLE_VERTEX_SPV}
    DEPENDS ${MODEL_EXAMPLE_VERTEX_SHADER}
    COMMENT "Compiling model example vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${MODEL_EXAMPLE_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MODEL_EXAMPLE_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${MODEL_EXAMPLE_FRAGMENT_SHADER} -o ${MODEL_EXAMPLE_FRAGMENT_SPV}
    DEPENDS ${MODEL_EXAMPLE_FRAGMENT_SHADER}
    COMMENT "Compiling model example fragment shader"
    VERBATIM
)

add_custom_target(model_example_shaders
    DEPENDS
        ${MODEL_EXAMPLE_VERTEX_SPV}
        ${MODEL_EXAMPLE_FRAGMENT_SPV}
)

add_dependencies(model_example model_example_shaders)
target_compile_definitions(model_example PRIVATE
    MODEL_EXAMPLE_SHADER_DIR="${MODEL_EXAMPLE_SHADER_OUTPUT_DIR}"
    MODEL_EXAMPLE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

set(MODEL_EXAMPLE_MODEL_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/data/pyramid.obj)
set(MODEL_EXAMPLE_TEXTURE_MANIFEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/data/texture_manifest.txt)
set(MODEL_EXAMPLE_TEXTURE_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/data/sandstone_brick.png)
set(MODEL_EXAMPLE_RUNTIME_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)

foreach(_asset
    ${MODEL_EXAMPLE_MODEL_SOURCE}
    ${MODEL_EXAMPLE_TEXTURE_MANIFEST_SOURCE}
    ${MODEL_EXAMPLE_TEXTURE_SOURCE}
)
    if(NOT EXISTS ${_asset})
        message(FATAL_ERROR "model_example missing required asset: ${_asset}")
    endif()
endforeach()

add_custom_command(TARGET model_example POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MODEL_EXAMPLE_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MODEL_EXAMPLE_MODEL_SOURCE}
        ${MODEL_EXAMPLE_RUNTIME_DATA_DIR}/pyramid.obj
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MODEL_EXAMPLE_TEXTURE_MANIFEST_SOURCE}
        ${MODEL_EXAMPLE_RUNTIME_DATA_DIR}/texture_manifest.txt
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MODEL_EXAMPLE_TEXTURE_SOURCE}
        ${MODEL_EXAMPLE_RUNTIME_DATA_DIR}/sandstone_brick.png
    COMMENT "Copying model example assets to runtime output directory"
)
