cmake_minimum_required(VERSION 3.21)
project(AOWIS-SERVER-GUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Widgets
    Network
)

if(NOT EMSCRIPTEN)
    find_package(Qt6 REQUIRED COMPONENTS
        Positioning
        SerialPort
    )
endif()

# GeographicLib submodule
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_BOTH_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(BUILD_MANPAGES OFF CACHE BOOL "" FORCE)
set(USE_BOOST OFF CACHE BOOL "" FORCE)

add_subdirectory(external/GeographicLib EXCLUDE_FROM_ALL)

qt_add_executable(aowis-server-gui
    resources.qrc
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
    
    
    
    src/rest_client.h src/rest_client.cpp
    src/menubar.h src/menubar.cpp
    src/map_widget.h src/map_widget.cpp
    src/footer_statusbar.h src/footer_statusbar.cpp
    src/_enums_structs.h
    src/tab_map_monitor_container.h src/tab_map_monitor_container.cpp
    src/tab_reservoirs_widget.h src/tab_reservoirs_widget.cpp
    src/tab_tanks_widget.h src/tab_tanks_widget.cpp
    src/tab_pumps_widget.h src/tab_pumps_widget.cpp
    src/tab_junctions_widget.h src/tab_junctions_widget.cpp
    src/tab_valves_widget.h src/tab_valves_widget.cpp
    src/tab_customer_points_widget.h src/tab_customer_points_widget.cpp
    src/tab_pipes_widget.h src/tab_pipes_widget.cpp
    src/tab_map_editor_container.h src/tab_map_editor_container.cpp
    src/tab_customers_widget.h src/tab_customers_widget.cpp
    src/tab_energy_widget.h src/tab_energy_widget.cpp
    src/map_navigation_widget.h src/map_navigation_widget.cpp
    src/map_model.h src/map_model.cpp
    src/map_controls.h src/map_controls.cpp
    src/map_network_canvas_widget.h src/map_network_canvas_widget.cpp
    src/tab_settings_widget.h src/tab_settings_widget.cpp
    src/_sizes.h
    src/geo_web_mercator.h src/geo_web_mercator.cpp
    src/geo_metric_projection.h src/geo_metric_projection.cpp
    src/interface_server_map.h 
    src/interface_server_map_rest.h src/interface_server_map_rest.cpp
)

if(APPLE)
    set_target_properties(aowis-server-gui PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "AOWIS Controller [Standalone]"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.aowis.controller"
    )
endif()

if(AOWIS_STANDALONE_BUILD)
    target_sources(aowis-server-gui
        PRIVATE
            src/interface_server_map_standalone.h
            src/interface_server_map_standalone.cpp
    )
    
    target_compile_definitions(aowis-server-gui
        PRIVATE
            AOWIS_STANDALONE
    )
endif()

target_link_libraries(aowis-server-gui
    PRIVATE
        Qt6::Widgets
        Qt6::Network
        GeographicLib_STATIC
)

if(NOT EMSCRIPTEN)
    target_link_libraries(aowis-server-gui
        PRIVATE
            Qt6::Positioning
            Qt6::SerialPort
    )
    
    target_sources(aowis-server-gui
        PRIVATE
            src/gps_provider.h
            src/gps_provider.cpp
    )
    
    target_compile_definitions(aowis-server-gui
        PRIVATE
            AOWIS_HAS_SERIAL_GPS=1
    )
else()
    target_sources(aowis-server-gui
        PRIVATE
            src/gps_provider_dummy.h
    )
    target_compile_definitions(aowis-server-gui
        PRIVATE
            AOWIS_HAS_SERIAL_GPS=0
    )
endif()

qt_import_plugins(aowis-server-gui
    INCLUDE_BY_TYPE imageformats
)
