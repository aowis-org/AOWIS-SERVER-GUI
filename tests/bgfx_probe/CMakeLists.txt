# An independent stacking probe; deliberately not part of the map renderer.
qt_add_executable(aowis-bgfx-probe
    main.cpp
)
target_link_libraries(aowis-bgfx-probe PRIVATE Qt6::Widgets bgfx)

if(UNIX AND NOT APPLE AND NOT EMSCRIPTEN AND TARGET Qt6::GuiPrivate)
    target_link_libraries(aowis-bgfx-probe PRIVATE Qt6::GuiPrivate)
    target_compile_definitions(aowis-bgfx-probe PRIVATE AOWIS_BGFX_NATIVE_WAYLAND_PRIVATE=1)
endif()

if(EMSCRIPTEN)
    target_link_options(aowis-bgfx-probe PRIVATE
        "-sMIN_WEBGL_VERSION=2"
        "-sMAX_WEBGL_VERSION=2"
    )
endif()

set_target_properties(aowis-bgfx-probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/bgfx_probe")
