add_executable(moon
    ${CMAKE_SOURCE_DIR}/examples/moon/moon.cpp
)
target_link_libraries(moon PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(MOON_SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/examples/model_example/shaders)
set(MOON_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(MOON_VERTEX_SHADER ${MOON_SHADER_SOURCE_DIR}/model.vert)
set(MOON_DEFAULT_FRAGMENT_SHADER ${MOON_SHADER_SOURCE_DIR}/model.frag)
set(MOON_MOON_FRAGMENT_SHADER ${CMAKE_CURRENT_SOURCE_DIR}/shaders/moon.frag)
set(MOON_VERTEX_SPV ${MOON_SHADER_OUTPUT_DIR}/model.vert.spv)
set(MOON_DEFAULT_FRAGMENT_SPV ${MOON_SHADER_OUTPUT_DIR}/model.frag.spv)
set(MOON_MOON_FRAGMENT_SPV ${MOON_SHADER_OUTPUT_DIR}/moon.frag.spv)

add_custom_command(
    OUTPUT ${MOON_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MOON_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${MOON_VERTEX_SHADER} -o ${MOON_VERTEX_SPV}
    DEPENDS ${MOON_VERTEX_SHADER}
    COMMENT "Compiling moon vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${MOON_DEFAULT_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MOON_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${MOON_DEFAULT_FRAGMENT_SHADER} -o ${MOON_DEFAULT_FRAGMENT_SPV}
    DEPENDS ${MOON_DEFAULT_FRAGMENT_SHADER}
    COMMENT "Compiling moon default fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${MOON_MOON_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MOON_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${MOON_MOON_FRAGMENT_SHADER} -o ${MOON_MOON_FRAGMENT_SPV}
    DEPENDS ${MOON_MOON_FRAGMENT_SHADER}
    COMMENT "Compiling moon fragment shader"
    VERBATIM
)

add_custom_target(moon_shaders
    DEPENDS
        ${MOON_VERTEX_SPV}
        ${MOON_DEFAULT_FRAGMENT_SPV}
        ${MOON_MOON_FRAGMENT_SPV}
)

add_dependencies(moon moon_shaders)
target_compile_definitions(moon PRIVATE
    MOON_ASSET_DIR="$<TARGET_FILE_DIR:moon>"
)

set(MOON_MODEL_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/data/moon.obj)
set(MOON_MTL_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/data/moon.mtl)
set(MOON_TEXTURE_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/data/moon.png)
set(MOON_PYRAMID_MODEL_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/data/pyramid.obj)
set(MOON_PYRAMID_MTL_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/data/pyramid.mtl)
set(MOON_STAR_TEXTURE_SOURCE ${CMAKE_SOURCE_DIR}/examples/asteroids3d/data/star.png)
set(MOON_RUNTIME_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)
set(MOON_RUNTIME_MODEL ${MOON_RUNTIME_DATA_DIR}/moon.obj)
set(MOON_RUNTIME_MTL ${MOON_RUNTIME_DATA_DIR}/moon.mtl)
set(MOON_RUNTIME_TEXTURE ${MOON_RUNTIME_DATA_DIR}/moon.png)
set(MOON_RUNTIME_STAR_TEXTURE ${MOON_RUNTIME_DATA_DIR}/star.png)
set(MOON_RUNTIME_VERTEX_SPV ${MOON_RUNTIME_DATA_DIR}/model.vert.spv)
set(MOON_RUNTIME_DEFAULT_FRAGMENT_SPV ${MOON_RUNTIME_DATA_DIR}/model.frag.spv)
set(MOON_RUNTIME_MOON_FRAGMENT_SPV ${MOON_RUNTIME_DATA_DIR}/moon.frag.spv)

foreach(_asset
    ${MOON_MODEL_SOURCE}
    ${MOON_MTL_SOURCE}
    ${MOON_TEXTURE_SOURCE}
    ${MOON_PYRAMID_MODEL_SOURCE}
    ${MOON_PYRAMID_MTL_SOURCE}
    ${MOON_STAR_TEXTURE_SOURCE}
    ${MOON_VERTEX_SHADER}
    ${MOON_DEFAULT_FRAGMENT_SHADER}
    ${MOON_MOON_FRAGMENT_SHADER}
)
    if(NOT EXISTS ${_asset})
        message(FATAL_ERROR "moon missing required asset: ${_asset}")
    endif()
endforeach()

add_custom_command(
    OUTPUT
        ${MOON_RUNTIME_MODEL}
        ${MOON_RUNTIME_MTL}
        ${MOON_RUNTIME_TEXTURE}
        ${MOON_RUNTIME_STAR_TEXTURE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MOON_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MOON_MODEL_SOURCE}
        ${MOON_RUNTIME_MODEL}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MOON_MTL_SOURCE}
        ${MOON_RUNTIME_MTL}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MOON_STAR_TEXTURE_SOURCE}
        ${MOON_RUNTIME_STAR_TEXTURE}
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        ${MOON_RUNTIME_DATA_DIR}
    COMMENT "Copying moon example assets to runtime output directory"
    DEPENDS
        ${MOON_MODEL_SOURCE}
        ${MOON_MTL_SOURCE}
        ${MOON_TEXTURE_SOURCE}
        ${MOON_PYRAMID_MODEL_SOURCE}
        ${MOON_PYRAMID_MTL_SOURCE}
        ${MOON_STAR_TEXTURE_SOURCE}
    VERBATIM
)

add_custom_target(moon_runtime_assets
    DEPENDS
        ${MOON_RUNTIME_MODEL}
        ${MOON_RUNTIME_MTL}
        ${MOON_RUNTIME_TEXTURE}
        ${MOON_RUNTIME_STAR_TEXTURE}
)

add_dependencies(moon moon_runtime_assets)

add_custom_command(
    OUTPUT
        ${MOON_RUNTIME_VERTEX_SPV}
        ${MOON_RUNTIME_DEFAULT_FRAGMENT_SPV}
        ${MOON_RUNTIME_MOON_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MOON_VERTEX_SPV}
        ${MOON_RUNTIME_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MOON_DEFAULT_FRAGMENT_SPV}
        ${MOON_RUNTIME_DEFAULT_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${MOON_MOON_FRAGMENT_SPV}
        ${MOON_RUNTIME_MOON_FRAGMENT_SPV}
    DEPENDS
        ${MOON_VERTEX_SPV}
        ${MOON_DEFAULT_FRAGMENT_SPV}
        ${MOON_MOON_FRAGMENT_SPV}
    COMMENT "Copying moon shaders to runtime output directory"
    VERBATIM
)

add_custom_target(moon_runtime_shaders
    DEPENDS
        ${MOON_RUNTIME_VERTEX_SPV}
        ${MOON_RUNTIME_DEFAULT_FRAGMENT_SPV}
        ${MOON_RUNTIME_MOON_FRAGMENT_SPV}
)

add_dependencies(moon moon_runtime_shaders)
