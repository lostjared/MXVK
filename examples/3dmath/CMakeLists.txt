add_executable(math3d
    ${CMAKE_SOURCE_DIR}/examples/3dmath/main.cpp
)
set_target_properties(math3d PROPERTIES OUTPUT_NAME "3dmath")
target_link_libraries(math3d PRIVATE mxvk::mxvk)
mxvk_configure_3dmath_backend(math3d)
