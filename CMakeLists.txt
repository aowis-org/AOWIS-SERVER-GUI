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
    src/enums_structs.h
    src/map_monitor_container.h src/map_monitor_container.cpp
    src/reservoirs_widget.h src/reservoirs_widget.cpp
    src/tanks_widget.h src/tanks_widget.cpp
    src/pumps_widget.h src/pumps_widget.cpp
    src/junctions_widget.h src/junctions_widget.cpp
    src/valves_widget.h src/valves_widget.cpp
    src/customer_points_widget.h src/customer_points_widget.cpp
    src/pipes_widget.h src/pipes_widget.cpp
    src/map_editor_container.h src/map_editor_container.cpp
    src/customers_widget.h src/customers_widget.cpp
    src/energy_widget.h src/energy_widget.cpp
    
)

target_link_libraries(aowis-server-gui PRIVATE Qt6::Widgets Qt6::Network)

qt_import_plugins(aowis-server-gui
    INCLUDE_BY_TYPE imageformats
)
