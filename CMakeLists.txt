cmake_minimum_required(VERSION 3.21)
project(AOWIS-SERVER-GUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_add_executable(aowis-server-gui
    resources.qrc
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
    
    src/utils/mercator.h src/utils/mercator.cpp
    
    src/rest_client.h src/rest_client.cpp
    src/menubar.h src/menubar.cpp
    src/map_widget.h src/map_widget.cpp
    src/footer_statusbar.h src/footer_statusbar.cpp
    
)

target_link_libraries(aowis-server-gui PRIVATE Qt6::Widgets Qt6::Network)

qt_import_plugins(aowis-server-gui
    INCLUDE_BY_TYPE imageformats
)
