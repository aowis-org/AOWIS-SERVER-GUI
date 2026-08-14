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
    Sql
)

if(EMSCRIPTEN)
    include(CheckCXXSourceCompiles)

    set(CMAKE_REQUIRED_LIBRARIES Qt6::Core)
    check_cxx_source_compiles(
        "#include <QtCore/qglobal.h>
#if !QT_CONFIG(thread)
#error Qt WebAssembly thread support is disabled
#endif
int main() { return 0; }"
        AOWIS_QT_WASM_HAS_THREAD_SUPPORT
    )
    unset(CMAKE_REQUIRED_LIBRARIES)

    if(NOT AOWIS_QT_WASM_HAS_THREAD_SUPPORT)
        message(FATAL_ERROR
            "The WebAssembly build requires a thread-enabled Qt WebAssembly kit. "
            "Install/use the Qt 'WebAssembly (multi-threaded)' component or build Qt "
            "for wasm with -feature-thread."
        )
    endif()
endif()

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

# AOWIS-SERVER-MODEL submodule
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/AOWIS-SERVER-MODEL/CMakeLists.txt")
    message(FATAL_ERROR
        "AOWIS-SERVER-MODEL submodule is missing. Run:\n"
        "  git submodule update --init --recursive"
    )
endif()
add_subdirectory(external/AOWIS-SERVER-MODEL EXCLUDE_FROM_ALL)

# AOWIS-SERVER-EPANET submodule
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/AOWIS-SERVER-EPANET/CMakeLists.txt")
    message(FATAL_ERROR
        "AOWIS-SERVER-EPANET submodule is missing. Run:\n"
        "  git submodule update --init --recursive"
    )
endif()
set(AOWIS_SERVER_EPANET_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(AOWIS_SERVER_EPANET_BUILD_SHARED_EPANET OFF CACHE BOOL "" FORCE)
add_subdirectory(external/AOWIS-SERVER-EPANET EXCLUDE_FROM_ALL)

# AOWIS-SERVER-DB submodule
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/AOWIS-SERVER-DB/CMakeLists.txt")
    message(FATAL_ERROR
        "AOWIS-SERVER-DB submodule is missing. Run:\n"
        "  git submodule update --init --recursive"
    )
endif()
add_subdirectory(external/AOWIS-SERVER-DB EXCLUDE_FROM_ALL)

qt_add_executable(aowis-server-gui
    resources.qrc
    src/main.cpp
    src/main_window.cpp
    src/main_window.h
    src/rest_client.h src/rest_client.cpp
    src/map_server_client_configuration.h src/map_server_client_configuration.cpp
    src/menubar.h src/menubar.cpp
    src/map/map_widget.h src/map/map_widget.cpp
    src/map/map_tile_repository.h src/map/map_tile_repository.cpp
    src/footer_statusbar.h src/footer_statusbar.cpp
    src/_enums_structs.h
    src/infrastructure_entity_traits.h
    src/hydraulic_entity_table_widget.h src/hydraulic_entity_table_widget.cpp
    src/network_symbology.h
    src/network_symbology_values.h
    src/network_render_snapshot.h
    src/network_render_snapshot_builder.h src/network_render_snapshot_builder.cpp
    src/wasm/browser_network_snapshot_serializer.h src/wasm/browser_network_snapshot_serializer.cpp
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
    src/map/map_navigation_widget.h src/map/map_navigation_widget.cpp
    src/map/map_model.h src/map/map_model.cpp
    src/map/map_canvas_widget.h src/map/map_canvas_widget.cpp
    src/map/map_editor_controller.h src/map/map_editor_controller.cpp
    src/map/map_editor_visual_state.h
    src/map/map_editor_renderer.h src/map/map_editor_renderer.cpp
    src/map/map_render_cache_math.h src/map/map_render_cache_math.cpp
    src/map/map_vector_document.h src/map/map_vector_document.cpp
    src/map/map_retained_vector_renderer.h src/map/map_retained_vector_renderer.cpp
    src/tab_settings_widget.h src/tab_settings_widget.cpp
    src/_sizes.h
    src/geo_web_mercator.h src/geo_web_mercator.cpp
    src/geo_metric_projection.h src/geo_metric_projection.cpp
    src/interface_server_map.h 
    src/interface_server_map_rest.h src/interface_server_map_rest.cpp
    src/tab_logs_widget.h src/tab_logs_widget.cpp
    src/tab_alarms_widget.h src/tab_alarms_widget.cpp
    src/map/map_models.h
    src/map/map_canvas_entities.h src/map/map_canvas_entities.cpp
    src/entity_inspector/entity_inspector_dock.h src/entity_inspector/entity_inspector_dock.cpp
    src/widgets/group_box_collapsible.h src/widgets/group_box_collapsible.cpp
    src/entity_inspector/entity_inspector_tank.h src/entity_inspector/entity_inspector_tank.cpp
    src/top_control_bar.h src/top_control_bar.cpp
    
    src/widgets/combo_checkboxes.h src/widgets/combo_checkboxes.cpp
    src/entity_inspector/entity_inspector_widget.h src/entity_inspector/entity_inspector_widget.cpp
    src/entity_inspector/entity_inspector_junction.h src/entity_inspector/entity_inspector_junction.cpp
    src/entity_inspector/entity_inspector_pipe.h src/entity_inspector/entity_inspector_pipe.cpp
    src/entity_inspector/entity_inspector_pump.h src/entity_inspector/entity_inspector_pump.cpp
    src/entity_inspector/entity_inspector_valve.h src/entity_inspector/entity_inspector_valve.cpp
    src/entity_inspector/entity_inspector_reservoir.h src/entity_inspector/entity_inspector_reservoir.cpp
    src/entity_inspector/entity_inspector_customer_point.h src/entity_inspector/entity_inspector_customer_point.cpp
    src/simulation_manager.h src/simulation_manager.cpp
    src/simulation_statistics_dialog.h src/simulation_statistics_dialog.cpp
    src/simulation_diagnostics_dialog.h src/simulation_diagnostics_dialog.cpp
    src/entity_inspector/entity_map_legend_dock.h src/entity_inspector/entity_map_legend_dock.cpp
    src/map/map_editor_guide_dock.h src/map/map_editor_guide_dock.cpp
    src/hydraulic_data.h src/hydraulic_data.cpp
    src/hydraulic_network_editor.h src/hydraulic_network_editor.cpp
    src/map/map_canvas_pipes.h src/map/map_canvas_pipes.cpp
    src/map/map_canvas_devicelinks.h src/map/map_canvas_devicelinks.cpp
    src/map/map_canvas_markers.h src/map/map_canvas_markers.cpp
    src/map/map_entity_pixmap_renderer.h src/map/map_entity_pixmap_renderer.cpp
    src/map/map_canvas_selection.h src/map/map_canvas_selection.cpp
    src/map/map_canvas_placement.h src/map/map_canvas_placement.cpp
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
        Qt6::Sql
        AOWIS::ServerModel
        AOWIS::ServerDBGUI
        AOWIS::ServerEpanet
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
            src/map/map_network_overlay_widget.h
            src/map/map_network_overlay_widget.cpp
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
