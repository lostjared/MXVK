add_executable(math3d_pong
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

target_link_libraries(math3d_pong PRIVATE mxvk::mxvk)
mxvk_configure_3dmath_backend(math3d_pong)

set_target_properties(math3d_pong PROPERTIES
    OUTPUT_NAME "3dmath_pong"
)
