add_executable(stencil_surface ${CMAKE_SOURCE_DIR}/examples/stencil_surface/surface.cpp)
target_link_libraries(stencil_surface PRIVATE mxvk::mxvk)
target_compile_definitions(stencil_surface PRIVATE skeleton_ASSET_DIR="$<TARGET_FILE_DIR:stencil_surface>"
)
