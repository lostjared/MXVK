find_package(Qt6 COMPONENTS Core Widgets Network REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

add_executable(relay_client
    main.cpp
    relaywindow.cpp
)

target_link_libraries(relay_client
    PRIVATE Qt6::Core Qt6::Widgets Qt6::Network
)
