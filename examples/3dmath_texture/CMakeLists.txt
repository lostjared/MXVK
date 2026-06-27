add_executable(math3d_texture
    ${CMAKE_SOURCE_DIR}/examples/3dmath_texture/main.cpp
)
set_target_properties(math3d_texture PROPERTIES OUTPUT_NAME "3dmath_texture")
target_link_libraries(math3d_texture PRIVATE mxvk::mxvk)
