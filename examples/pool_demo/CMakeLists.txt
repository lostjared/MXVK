if(NOT TARGET SDL3::SDL3)
    find_package(SDL3 REQUIRED)
endif()
if(NOT TARGET SDL3_ttf::SDL3_ttf)
    find_package(SDL3_ttf REQUIRED)
endif()
find_package(Threads REQUIRED)

if(MINGW)
    set(WIN_RC "${CMAKE_CURRENT_SOURCE_DIR}/ball.rc")
    add_executable(Pool3D WIN32 main.cpp ${WIN_RC})
else()
    add_executable(Pool3D main.cpp)
endif()

set_target_properties(Pool3D PROPERTIES
    OUTPUT_NAME "3DPool"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)

find_program(GLSLC_EXECUTABLE glslc)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Please install Vulkan SDK shader tools.")
endif()

set(POOL_DEMO_SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(POOL_DEMO_SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(POOL_DEMO_VERTEX_SHADER ${POOL_DEMO_SHADER_SOURCE_DIR}/vertex.vert)
set(POOL_DEMO_FRAGMENT_SHADER ${POOL_DEMO_SHADER_SOURCE_DIR}/fragment.frag)
set(POOL_DEMO_SPRITE_VERTEX_SHADER ${POOL_DEMO_SHADER_SOURCE_DIR}/sprite_vertex.vert)
set(POOL_DEMO_VERTEX_SPV ${POOL_DEMO_SHADER_OUTPUT_DIR}/model.vert.spv)
set(POOL_DEMO_FRAGMENT_SPV ${POOL_DEMO_SHADER_OUTPUT_DIR}/model.frag.spv)
set(POOL_DEMO_SPRITE_VERTEX_SPV ${POOL_DEMO_SHADER_OUTPUT_DIR}/sprite.vert.spv)

add_custom_command(
    OUTPUT ${POOL_DEMO_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${POOL_DEMO_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${POOL_DEMO_VERTEX_SHADER} -o ${POOL_DEMO_VERTEX_SPV}
    DEPENDS ${POOL_DEMO_VERTEX_SHADER}
    COMMENT "Compiling pool demo vertex shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${POOL_DEMO_FRAGMENT_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${POOL_DEMO_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${POOL_DEMO_FRAGMENT_SHADER} -o ${POOL_DEMO_FRAGMENT_SPV}
    DEPENDS ${POOL_DEMO_FRAGMENT_SHADER}
    COMMENT "Compiling pool demo fragment shader"
    VERBATIM
)

add_custom_command(
    OUTPUT ${POOL_DEMO_SPRITE_VERTEX_SPV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${POOL_DEMO_SHADER_OUTPUT_DIR}
    COMMAND ${GLSLC_EXECUTABLE} ${POOL_DEMO_SPRITE_VERTEX_SHADER} -o ${POOL_DEMO_SPRITE_VERTEX_SPV}
    DEPENDS ${POOL_DEMO_SPRITE_VERTEX_SHADER}
    COMMENT "Compiling pool demo sprite vertex shader"
    VERBATIM
)

add_custom_target(pool_demo_shaders
    DEPENDS
        ${POOL_DEMO_VERTEX_SPV}
        ${POOL_DEMO_FRAGMENT_SPV}
        ${POOL_DEMO_SPRITE_VERTEX_SPV}
)

add_dependencies(Pool3D pool_demo_shaders)

target_compile_definitions(Pool3D PRIVATE
    POOL_DEMO_ASSET_DIR="${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(Pool3D PRIVATE
    mxvk::mxvk
    SDL3::SDL3
    SDL3_ttf::SDL3_ttf
    Threads::Threads
)

add_custom_command(TARGET Pool3D POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/data"
        "${CMAKE_CURRENT_BINARY_DIR}/data"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${POOL_DEMO_SPRITE_VERTEX_SPV}
        "${CMAKE_CURRENT_BINARY_DIR}/data/sprite.vert.spv"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${POOL_DEMO_SPRITE_VERTEX_SPV}
        "${CMAKE_CURRENT_BINARY_DIR}/data/sprite_vert.spv"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${POOL_DEMO_VERTEX_SPV}
        "${CMAKE_CURRENT_BINARY_DIR}/data/model.vert.spv"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${POOL_DEMO_FRAGMENT_SPV}
        "${CMAKE_CURRENT_BINARY_DIR}/data/model.frag.spv"
    COMMENT "Copying data/ assets to pool_demo build directory"
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/font.ttf")
    add_custom_command(TARGET Pool3D POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/font.ttf"
            "${CMAKE_CURRENT_BINARY_DIR}/font.ttf"
        COMMENT "Copying font.ttf to pool_demo build directory"
    )
endif()

#install(TARGETS Pool3D RUNTIME DESTINATION bin)
#install(DIRECTORY data/ DESTINATION share/3DPool/data)
#install(FILES font.ttf DESTINATION share/3DPool)
