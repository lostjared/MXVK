add_executable(binary_matrix
    ${CMAKE_SOURCE_DIR}/examples/binary_matrix/binary_matrix.cpp
)

target_link_libraries(binary_matrix PRIVATE mxvk::mxvk)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(binary_matrix_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(binary_matrix_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(binary_matrix_BACKGROUND_FRAGMENT_SHADER ${binary_matrix_SHADER_SOURCE_DIR}/background.frag)
set(binary_matrix_BACKGROUND_FRAGMENT_SPV ${binary_matrix_SHADER_OUTPUT_DIR}/background.frag.spv)

add_custom_command(
    OUTPUT ${binary_matrix_BACKGROUND_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${binary_matrix_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${binary_matrix_BACKGROUND_FRAGMENT_SHADER} -o ${binary_matrix_BACKGROUND_FRAGMENT_SPV}
    DEPENDS ${binary_matrix_BACKGROUND_FRAGMENT_SHADER}
    COMMENT "Compiling binary_matrix background fragment shader"
    VERBATIM
)

add_custom_target(binary_matrix_shaders
    DEPENDS ${binary_matrix_BACKGROUND_FRAGMENT_SPV}
)

add_dependencies(binary_matrix binary_matrix_shaders)

target_compile_definitions(binary_matrix PRIVATE
    binary_matrix_ASSET_DIR="$<TARGET_FILE_DIR:binary_matrix>"
)

add_custom_command(TARGET binary_matrix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:binary_matrix>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/matrix/data/keifont.ttf
        $<TARGET_FILE_DIR:binary_matrix>/data/keifont.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/examples/matrix/data/bg.png
        $<TARGET_FILE_DIR:binary_matrix>/data/bg.png
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${binary_matrix_BACKGROUND_FRAGMENT_SPV}
        $<TARGET_FILE_DIR:binary_matrix>/data/background.frag.spv
    COMMENT "Copying binary_matrix assets to runtime output directory"
)
