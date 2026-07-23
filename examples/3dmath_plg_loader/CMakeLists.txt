add_executable(math3d_plg_loader
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

target_link_libraries(math3d_plg_loader PRIVATE mxvk::mxvk)
mxvk_configure_3dmath_backend(math3d_plg_loader)

set_target_properties(math3d_plg_loader PROPERTIES
    OUTPUT_NAME "3dmath_plg_loader"
)

set(math3d_plg_loader_RUNTIME_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)
set(math3d_plg_loader_SPHERE_MODEL ${CMAKE_CURRENT_SOURCE_DIR}/data/sphere.plg)

add_custom_target(math3d_plg_loader_runtime_data
    COMMAND ${CMAKE_COMMAND} -E make_directory ${math3d_plg_loader_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${math3d_plg_loader_SPHERE_MODEL}
        ${math3d_plg_loader_RUNTIME_DATA_DIR}/sphere.plg
    DEPENDS ${math3d_plg_loader_SPHERE_MODEL}
    COMMENT "Copying 3D Math PLG Loader runtime data"
    VERBATIM
)

add_dependencies(math3d_plg_loader math3d_plg_loader_runtime_data)
