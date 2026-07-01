add_executable(surface ${CMAKE_SOURCE_DIR}/examples/surface/surface.cpp)
target_link_libraries(surface PRIVATE mxvk::mxvk)
target_compile_definitions(surface PRIVATE skeleton_ASSET_DIR="$<TARGET_FILE_DIR:surface>"
)
