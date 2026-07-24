# Keep OBJ and PLG interaction, rasterization, and benchmark paths identical.
add_executable(math3d_obj_loader
    ${CMAKE_CURRENT_SOURCE_DIR}/../3dmath_plg_loader/main.cpp
)

target_compile_definitions(math3d_obj_loader PRIVATE MXVK_OBJ_LOADER)
target_link_libraries(math3d_obj_loader PRIVATE mxvk::mxvk)
mxvk_configure_3dmath_backend(math3d_obj_loader)

set_target_properties(math3d_obj_loader PROPERTIES
    OUTPUT_NAME "3dmath_obj_loader"
)

set(math3d_obj_loader_RUNTIME_DATA_DIR ${CMAKE_CURRENT_BINARY_DIR}/data)
set(math3d_obj_loader_SPHERE_MODEL ${PROJECT_SOURCE_DIR}/models/obj/sphere.obj)
set(math3d_obj_loader_SPHERE_MATERIAL ${PROJECT_SOURCE_DIR}/models/obj/sphere.mtl)

add_custom_target(math3d_obj_loader_runtime_data
    COMMAND ${CMAKE_COMMAND} -E make_directory ${math3d_obj_loader_RUNTIME_DATA_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${math3d_obj_loader_SPHERE_MODEL}
        ${math3d_obj_loader_RUNTIME_DATA_DIR}/sphere.obj
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${math3d_obj_loader_SPHERE_MATERIAL}
        ${math3d_obj_loader_RUNTIME_DATA_DIR}/sphere.mtl
    DEPENDS
        ${math3d_obj_loader_SPHERE_MODEL}
        ${math3d_obj_loader_SPHERE_MATERIAL}
    COMMENT "Copying 3D Math OBJ Loader runtime data"
    VERBATIM
)

add_dependencies(math3d_obj_loader math3d_obj_loader_runtime_data)
