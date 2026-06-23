add_executable(skeleton
    ${CMAKE_SOURCE_DIR}/examples/skeleton/skeleton.cpp
)
target_link_libraries(skeleton PRIVATE mxvk::mxvk)
target_compile_definitions(skeleton PRIVATE
    skeleton_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

