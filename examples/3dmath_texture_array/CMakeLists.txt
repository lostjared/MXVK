add_executable(math3d_texture_array
    ${CMAKE_SOURCE_DIR}/examples/3dmath_texture_array/main.cpp
)
set_target_properties(math3d_texture_array PROPERTIES OUTPUT_NAME "3dmath_texture_array")
target_link_libraries(math3d_texture_array PRIVATE mxvk::mxvk)
mxvk_configure_3dmath_backend(math3d_texture_array)
