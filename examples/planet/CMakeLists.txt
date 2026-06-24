add_executable(planet
    ${CMAKE_SOURCE_DIR}/examples/planet/planet.cpp
)
target_link_libraries(planet PRIVATE rain)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(PLANET_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(PLANET_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(PLANET_VERTEX_SHADER ${PLANET_SHADER_SOURCE_DIR}/model.vert)
set(PLANET_FRAGMENT_SHADER ${PLANET_SHADER_SOURCE_DIR}/model.frag)
set(PLANET_VERTEX_SPV ${PLANET_SHADER_OUTPUT_DIR}/model.vert.spv)
set(PLANET_FRAGMENT_SPV ${PLANET_SHADER_OUTPUT_DIR}/model.frag.spv)

add_custom_command(
    OUTPUT ${PLANET_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PLANET_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${PLANET_VERTEX_SHADER} -o ${PLANET_VERTEX_SPV}
    DEPENDS ${PLANET_VERTEX_SHADER}
    COMMENT "Compiling planet vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${PLANET_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PLANET_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${PLANET_FRAGMENT_SHADER} -o ${PLANET_FRAGMENT_SPV}
    DEPENDS ${PLANET_FRAGMENT_SHADER}
    COMMENT "Compiling planet fragment shader"
    VERBATIM
)

add_custom_target(planet_shaders
    DEPENDS
        ${PLANET_VERTEX_SPV}
        ${PLANET_FRAGMENT_SPV}
)

add_dependencies(planet planet_shaders)
target_compile_definitions(planet PRIVATE
    PLANET_SHADER_DIR="${PLANET_SHADER_OUTPUT_DIR}"
    PLANET_ASSET_DIR="$<TARGET_FILE_DIR:planet>"
)

foreach(_asset
    ${CMAKE_CURRENT_SOURCE_DIR}/data/saturn.mxmod.z
    ${CMAKE_CURRENT_SOURCE_DIR}/data/saturn.tex
    ${CMAKE_CURRENT_SOURCE_DIR}/data/surface.png
    ${CMAKE_CURRENT_SOURCE_DIR}/data/ring.png
    ${CMAKE_SOURCE_DIR}/examples/rain/data/NotoSansCJK-Bold.ttc
    ${CMAKE_SOURCE_DIR}/examples/rain/data/NotoSansCJK-Regular.ttc
    ${CMAKE_CURRENT_SOURCE_DIR}/data/binary.ttf
    ${PLANET_VERTEX_SHADER}
    ${PLANET_FRAGMENT_SHADER}
)
    if(NOT EXISTS ${_asset})
        message(FATAL_ERROR "planet missing required asset: ${_asset}")
    endif()
endforeach()

add_custom_command(TARGET planet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:planet>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:planet>/data
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/examples/rain/data/NotoSansCJK-Bold.ttc
        $<TARGET_FILE_DIR:planet>/data/NotoSansCJK-Bold.ttc
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_SOURCE_DIR}/examples/rain/data/NotoSansCJK-Regular.ttc
        $<TARGET_FILE_DIR:planet>/data/NotoSansCJK-Regular.ttc
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_SOURCE_DIR}/data/binary.ttf
        $<TARGET_FILE_DIR:planet>/data/binary.ttf
    COMMENT "Copying planet example assets to runtime output directory"
)
