
add_library(volk STATIC volk.cpp)
set_target_properties(volk PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(volk PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(NOT WIN32)
    target_link_libraries(volk PUBLIC ${CMAKE_DL_LIBS})
endif()

target_link_libraries(volk PUBLIC Vulkan::Vulkan)

add_library(libmxvk::volk ALIAS volk)

install(TARGETS volk
    EXPORT mxvkTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/volk.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/volk
)
