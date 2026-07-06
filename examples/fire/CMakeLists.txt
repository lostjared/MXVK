add_executable(fire
    ${CMAKE_SOURCE_DIR}/examples/fire/fire.cpp
)
target_link_libraries(fire PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(FIRE_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(FIRE_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(FIRE_RUNTIME_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)

set(FIRE_FRAGMENT_SHADER ${FIRE_SHADER_SOURCE_DIR}/fire.frag)
set(FIRE_FRAGMENT_SPV ${FIRE_SHADER_OUTPUT_DIR}/fire.frag.spv)

add_custom_command(
    OUTPUT ${FIRE_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${FIRE_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.3 ${FIRE_FRAGMENT_SHADER} -o ${FIRE_FRAGMENT_SPV}
    DEPENDS ${FIRE_FRAGMENT_SHADER}
    COMMENT "Compiling fire fragment shader"
    VERBATIM
)

add_custom_target(fire_shaders
    COMMAND ${CMAKE_COMMAND} -E make_directory ${FIRE_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${FIRE_FRAGMENT_SPV}
        ${FIRE_RUNTIME_DATA_DIR}/fire.frag.spv
    DEPENDS
        ${FIRE_FRAGMENT_SPV}
    COMMENT "Copying fire SPIR-V shaders to runtime data directory"
    VERBATIM
)

add_dependencies(fire fire_shaders)
target_compile_definitions(fire PRIVATE
    FIRE_ASSET_DIR="$<TARGET_FILE_DIR:fire>"
)
