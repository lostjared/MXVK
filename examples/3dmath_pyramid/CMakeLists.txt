add_executable(math3d_pyramid
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

target_link_libraries(math3d_pyramid PRIVATE mxvk::mxvk)
mxvk_configure_3dmath_backend(math3d_pyramid)

set_target_properties(math3d_pyramid PROPERTIES
    OUTPUT_NAME "3dmath_pyramid"
)

set(math3d_pyramid_RUNTIME_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)
set(math3d_pyramid_MODEL ${CMAKE_CURRENT_SOURCE_DIR}/data/pyramid.plg)

add_custom_target(math3d_pyramid_runtime_data
    COMMAND ${CMAKE_COMMAND} -E make_directory ${math3d_pyramid_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${math3d_pyramid_MODEL}
        ${math3d_pyramid_RUNTIME_DATA_DIR}/pyramid.plg
    DEPENDS ${math3d_pyramid_MODEL}
    COMMENT "Copying 3D Math Pyramid runtime data"
    VERBATIM
)

add_dependencies(math3d_pyramid math3d_pyramid_runtime_data)
