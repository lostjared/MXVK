add_library(rain STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/rain.cpp
)

target_link_libraries(rain PUBLIC mxvk::mxvk)

target_include_directories(rain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
