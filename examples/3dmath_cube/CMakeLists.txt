add_executable(math3d_cube
    ${CMAKE_SOURCE_DIR}/examples/3dmath_cube/main.cpp
)
set_target_properties(math3d_cube PROPERTIES OUTPUT_NAME "3dmath_cube")
target_link_libraries(math3d_cube PRIVATE mxvk::mxvk)
