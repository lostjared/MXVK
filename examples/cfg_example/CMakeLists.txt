add_executable(cfg_example
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

target_link_libraries(cfg_example PRIVATE mxvk::mxvk)
