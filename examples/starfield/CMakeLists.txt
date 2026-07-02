add_executable(starfield
    ${CMAKE_SOURCE_DIR}/examples/starfield/starfield.cpp
)
target_link_libraries(starfield PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(STARFIELD_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(STARFIELD_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(STARFIELD_RUNTIME_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)

set(STARFIELD_VERTEX_SHADER ${STARFIELD_SHADER_SOURCE_DIR}/starfield.vert)
set(STARFIELD_FRAGMENT_SHADER ${STARFIELD_SHADER_SOURCE_DIR}/starfield.frag)
set(STARFIELD_VERTEX_SPV ${STARFIELD_SHADER_OUTPUT_DIR}/starfield.vert.spv)
set(STARFIELD_FRAGMENT_SPV ${STARFIELD_SHADER_OUTPUT_DIR}/starfield.frag.spv)

add_custom_command(
    OUTPUT ${STARFIELD_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${STARFIELD_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${STARFIELD_VERTEX_SHADER} -o ${STARFIELD_VERTEX_SPV}
    DEPENDS ${STARFIELD_VERTEX_SHADER}
    COMMENT "Compiling starfield vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${STARFIELD_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${STARFIELD_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${STARFIELD_FRAGMENT_SHADER} -o ${STARFIELD_FRAGMENT_SPV}
    DEPENDS ${STARFIELD_FRAGMENT_SHADER}
    COMMENT "Compiling starfield fragment shader"
    VERBATIM
)

add_custom_target(starfield_shaders
    COMMAND ${CMAKE_COMMAND} -E make_directory ${STARFIELD_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${STARFIELD_VERTEX_SPV}
        ${STARFIELD_RUNTIME_DATA_DIR}/starfield.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${STARFIELD_FRAGMENT_SPV}
        ${STARFIELD_RUNTIME_DATA_DIR}/starfield.frag.spv
    DEPENDS
        ${STARFIELD_VERTEX_SPV}
        ${STARFIELD_FRAGMENT_SPV}
    COMMENT "Copying starfield SPIR-V shaders to runtime data directory"
    VERBATIM
)

add_dependencies(starfield starfield_shaders)
target_compile_definitions(starfield PRIVATE
    STARFIELD_ASSET_DIR="$<TARGET_FILE_DIR:starfield>"
)

add_custom_command(TARGET starfield POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:starfield>/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/data
        $<TARGET_FILE_DIR:starfield>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${STARFIELD_VERTEX_SPV}
        $<TARGET_FILE_DIR:starfield>/data/starfield.vert.spv
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${STARFIELD_FRAGMENT_SPV}
        $<TARGET_FILE_DIR:starfield>/data/starfield.frag.spv
    COMMENT "Copying starfield assets to runtime output directory"
)
