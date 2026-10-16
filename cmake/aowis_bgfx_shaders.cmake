# Pre-compiled bgfx shaders for the Map Monitor.
#
# The compiled shaders are committed in src/map/bgfx/shaders/bin/<family>/,
# one directory per shaderc profile family (the directory names bgfx's own
# examples use, see map_bgfx_backend.h):
#
#   glsl   OpenGL 4.x desktop          (renderer OPENGL)
#   essl   OpenGL ES 3 / WebGL 2       (renderer OPENGLES, WASM)
#   spirv  Vulkan                      (renderer VULKAN)
#   metal  Metal                       (renderer METAL)
#   dx11   Direct3D 11, DXBC           (renderer DIRECT3D11; Windows host only)
#
# Normal desktop and WASM builds only embed these files: no shader compiler
# is built, which removes the largest part of bgfx's build (shaderc pulled in
# Tint/Dawn, SPIRV-Tools, glslang and SPIRV-Cross, ~690 translation units).
#
# Staleness guard: every family directory has inputs.sha256, the hash of
# everything its binaries were compiled from (all .sc files, the varying files,
# globe_network.sh,
# bgfx's bgfx_shader.sh, profile and platform; line endings normalised so a
# CRLF checkout hashes like LF). Editing a shader reconfigures CMake
# (CMAKE_CONFIGURE_DEPENDS) and a family whose hash no longer matches is
# never embedded: the build stops with the regeneration command instead of
# running old shaders against new C++ vertex layouts.
#
# Regeneration (developer only): configure with -DAOWIS_BGFX_SHADER_TOOLS=ON.
# That builds aowis-bgfx-shaderc (cmake/aowis_bgfx_shaderc.cmake) and
# compiles every family this host can produce into the build tree, then
# copies the binaries and inputs.sha256 into src/map/bgfx/shaders/bin for
# committing. Outputs live in the build tree, so "clean" never deletes
# committed files. Every host produces glsl, essl, spirv and metal; dx11
# comes from Microsoft's d3dcompiler_47.dll (bundled with bgfx), directly on
# Windows and under Wine elsewhere (AOWIS_BGFX_WINE: found on PATH, or set it).

set(AOWIS_BGFX_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/map/bgfx/shaders")
set(AOWIS_BGFX_SHADER_BIN_DIR "${AOWIS_BGFX_SHADER_DIR}/bin")
set(AOWIS_BGFX_SHADER_NAMES
    vs_network_2d vs_line_2d fs_network_2d fs_marker_2d
    vs_icon_2d fs_icon_2d vs_flow_2d fs_flow_2d
    vs_imagery_2d fs_imagery_2d
    fs_heatmap_2d
    vs_hud fs_hud
    vs_globe_surface fs_globe_surface vs_globe_wireframe fs_globe_wireframe
    vs_globe_link fs_globe_link fs_globe_link_xray vs_globe_flow
    vs_globe_node fs_globe_node vs_globe_junction fs_globe_junction fs_globe_junction_xray
    vs_globe_icon fs_globe_icon vs_globe_model fs_globe_model
    vs_globe_surface_heatmap fs_globe_surface_heatmap vs_globe_heatmap_bake fs_globe_heatmap_bake)
# Varying/attribute declarations: varying.def.sc unless a shader names its
# own here (the globe's positions are vec3; the 2D shaders keep theirs
# untouched, so their binaries do not change).
set(AOWIS_BGFX_SHADER_VARYINGS_vs_globe_surface varying_globe.def.sc)
set(AOWIS_BGFX_SHADER_VARYINGS_fs_globe_surface varying_globe.def.sc)
set(AOWIS_BGFX_SHADER_VARYINGS_vs_globe_wireframe varying_globe.def.sc)
set(AOWIS_BGFX_SHADER_VARYINGS_fs_globe_wireframe varying_globe.def.sc)
set(AOWIS_BGFX_SHADER_VARYINGS_vs_globe_surface_heatmap varying_globe.def.sc)
set(AOWIS_BGFX_SHADER_VARYINGS_fs_globe_surface_heatmap varying_globe.def.sc)
# The globe heatmap bake into atlas cells (map_bgfx_globe_surface.h).
set(AOWIS_BGFX_SHADER_VARYINGS_vs_globe_heatmap_bake varying_globe_heatmap.def.sc)
set(AOWIS_BGFX_SHADER_VARYINGS_fs_globe_heatmap_bake varying_globe_heatmap.def.sc)
# The globe network (map_bgfx_globe_network.h): its own vertex streams and
# varyings; shared uniforms and helpers in globe_network.sh.
foreach(aowis_shader_name
        vs_globe_link fs_globe_link fs_globe_link_xray vs_globe_flow
        vs_globe_node fs_globe_node vs_globe_junction fs_globe_junction fs_globe_junction_xray
        vs_globe_icon fs_globe_icon vs_globe_model fs_globe_model)
    set(AOWIS_BGFX_SHADER_VARYINGS_${aowis_shader_name} varying_globe_network.def.sc)
endforeach()
function(aowis_bgfx_shader_varyings shader_name out_variable)
    if(DEFINED AOWIS_BGFX_SHADER_VARYINGS_${shader_name})
        set(${out_variable} "${AOWIS_BGFX_SHADER_VARYINGS_${shader_name}}" PARENT_SCOPE)
    else()
        set(${out_variable} varying.def.sc PARENT_SCOPE)
    endif()
endfunction()
# <family>|<shaderc profile>|<shaderc platform>
set(AOWIS_BGFX_SHADER_FAMILIES
    "glsl|330|linux"
    "essl|300_es|asm.js"
    "spirv|spirv|linux"
    "metal|metal|osx"
    "dx11|s_5_0|windows")

option(AOWIS_BGFX_SHADER_TOOLS
    "Build the bgfx shader compiler and regenerate src/map/bgfx/shaders/bin" OFF)
if(EMSCRIPTEN AND AOWIS_BGFX_SHADER_TOOLS)
    message(FATAL_ERROR
        "AOWIS_BGFX_SHADER_TOOLS is a host build option. Regenerate the shaders "
        "with a native build, then run the WASM build.")
endif()

# Every file the binaries depend on, in a fixed order.
set(AOWIS_BGFX_SHADER_INPUTS "")
foreach(aowis_shader_name IN LISTS AOWIS_BGFX_SHADER_NAMES)
    list(APPEND AOWIS_BGFX_SHADER_INPUTS "${AOWIS_BGFX_SHADER_DIR}/${aowis_shader_name}.sc")
endforeach()
list(APPEND AOWIS_BGFX_SHADER_INPUTS
    "${AOWIS_BGFX_SHADER_DIR}/varying.def.sc"
    "${AOWIS_BGFX_SHADER_DIR}/varying_globe.def.sc"
    "${AOWIS_BGFX_SHADER_DIR}/varying_globe_network.def.sc"
    "${AOWIS_BGFX_SHADER_DIR}/varying_globe_heatmap.def.sc"
    "${AOWIS_BGFX_SHADER_DIR}/globe_network.sh"
    "${AOWIS_BGFX_DIR}/src/bgfx_shader.sh")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${AOWIS_BGFX_SHADER_INPUTS})

# Hash of all inputs plus profile/platform, independent of line endings.
function(aowis_bgfx_shader_inputs_hash profile platform out_variable)
    set(aowis_text "aowis-bgfx-shaders-v1|${profile}|${platform}\n")
    foreach(aowis_input IN LISTS AOWIS_BGFX_SHADER_INPUTS)
        file(READ "${aowis_input}" aowis_content)
        string(REPLACE "\r\n" "\n" aowis_content "${aowis_content}")
        get_filename_component(aowis_input_name "${aowis_input}" NAME)
        string(SHA256 aowis_content_hash "${aowis_content}")
        string(APPEND aowis_text "${aowis_input_name} ${aowis_content_hash}\n")
    endforeach()
    string(SHA256 aowis_hash "${aowis_text}")
    set(${out_variable} "${aowis_hash}" PARENT_SCOPE)
endfunction()

# dx11 on Linux/macOS: shaderc's d3d4linux runs the compiler under Wine.
if(AOWIS_BGFX_SHADER_TOOLS AND NOT CMAKE_HOST_WIN32)
    find_program(AOWIS_BGFX_WINE NAMES wine wine64
        DOC "Wine for compiling dx11 shaders (bgfx d3d4linux)")
endif()

# Whether shaderc on this host can produce a family.
function(aowis_bgfx_shader_family_producible family out_variable)
    if(family STREQUAL "dx11" AND NOT CMAKE_HOST_WIN32 AND NOT AOWIS_BGFX_WINE)
        set(${out_variable} FALSE PARENT_SCOPE)
    else()
        set(${out_variable} TRUE PARENT_SCOPE)
    endif()
endfunction()

function(aowis_bgfx_family_for_renderer renderer out_variable)
    if(renderer STREQUAL "OPENGL")
        set(${out_variable} glsl PARENT_SCOPE)
    elseif(renderer STREQUAL "OPENGLES")
        set(${out_variable} essl PARENT_SCOPE)
    elseif(renderer STREQUAL "VULKAN")
        set(${out_variable} spirv PARENT_SCOPE)
    elseif(renderer STREQUAL "DIRECT3D11")
        set(${out_variable} dx11 PARENT_SCOPE)
    elseif(renderer STREQUAL "METAL")
        set(${out_variable} metal PARENT_SCOPE)
    else()
        message(FATAL_ERROR "No shader family for bgfx renderer ${renderer}")
    endif()
endfunction()

# Embeds the shader families of AOWIS_BGFX_RENDERERS into target as Qt
# resources under :/aowis/map/bgfx/<family>/<name>.bin.
function(aowis_bgfx_add_shaders target)
    set(aowis_required_families "")
    foreach(aowis_renderer IN LISTS AOWIS_BGFX_RENDERERS)
        aowis_bgfx_family_for_renderer("${aowis_renderer}" aowis_family)
        list(APPEND aowis_required_families "${aowis_family}")
    endforeach()

    set(aowis_generated_root "${CMAKE_CURRENT_BINARY_DIR}/aowis-bgfx-shaders")
    set(aowis_regeneration_stamps "")
    set(aowis_embedded_families "")
    foreach(aowis_family_entry IN LISTS AOWIS_BGFX_SHADER_FAMILIES)
        string(REPLACE "|" ";" aowis_family_parts "${aowis_family_entry}")
        list(GET aowis_family_parts 0 aowis_family)
        list(GET aowis_family_parts 1 aowis_profile)
        list(GET aowis_family_parts 2 aowis_platform)
        aowis_bgfx_shader_family_producible("${aowis_family}" aowis_producible)
        list(FIND aowis_required_families "${aowis_family}" aowis_required_index)
        aowis_bgfx_shader_inputs_hash("${aowis_profile}" "${aowis_platform}" aowis_hash)

        set(aowis_committed_dir "${AOWIS_BGFX_SHADER_BIN_DIR}/${aowis_family}")
        set(aowis_committed_stamp "${aowis_committed_dir}/inputs.sha256")
        set(aowis_committed_hash "")
        if(EXISTS "${aowis_committed_stamp}")
            file(READ "${aowis_committed_stamp}" aowis_committed_hash)
            string(STRIP "${aowis_committed_hash}" aowis_committed_hash)
        endif()
        set(aowis_up_to_date FALSE)
        if(aowis_committed_hash STREQUAL aowis_hash)
            set(aowis_up_to_date TRUE)
            foreach(aowis_shader_name IN LISTS AOWIS_BGFX_SHADER_NAMES)
                if(NOT EXISTS "${aowis_committed_dir}/${aowis_shader_name}.bin")
                    set(aowis_up_to_date FALSE)
                endif()
            endforeach()
        endif()

        set(aowis_family_binaries "")
        if(AOWIS_BGFX_SHADER_TOOLS AND aowis_producible)
            # Compile into the build tree, then publish to the source tree.
            set(aowis_out_dir "${aowis_generated_root}/${aowis_family}")
            set(aowis_stamp_source "${aowis_generated_root}/${aowis_family}.inputs.sha256")
            # Rewritten only when the hash changes, so it triggers exactly
            # the recompilations that are needed.
            file(CONFIGURE OUTPUT "${aowis_stamp_source}" CONTENT "${aowis_hash}\n")
            set(aowis_optimize "")
            set(aowis_shaderc_launcher "")
            if(aowis_family STREQUAL "dx11")
                set(aowis_optimize -O 3)
                if(NOT CMAKE_HOST_WIN32)
                    # Wine quietly, in its own prefix inside the build tree.
                    set(aowis_shaderc_launcher "${CMAKE_COMMAND}" -E env
                        "D3D4LINUX_WINE=${AOWIS_BGFX_WINE}"
                        "WINEPREFIX=${CMAKE_BINARY_DIR}/aowis-bgfx-shaderc/wineprefix"
                        "WINEDEBUG=-all")
                endif()
            endif()
            set(aowis_commands COMMAND "${CMAKE_COMMAND}" -E make_directory
                "${aowis_out_dir}" "${aowis_committed_dir}")
            foreach(aowis_shader_name IN LISTS AOWIS_BGFX_SHADER_NAMES)
                if(aowis_shader_name MATCHES "^vs_")
                    set(aowis_type vertex)
                else()
                    set(aowis_type fragment)
                endif()
                aowis_bgfx_shader_varyings("${aowis_shader_name}" aowis_varyings)
                set(aowis_binary "${aowis_out_dir}/${aowis_shader_name}.bin")
                list(APPEND aowis_family_binaries "${aowis_binary}")
                list(APPEND aowis_commands
                    COMMAND ${aowis_shaderc_launcher} $<TARGET_FILE:aowis-bgfx-shaderc>
                        -f "${AOWIS_BGFX_SHADER_DIR}/${aowis_shader_name}.sc"
                        -o "${aowis_binary}"
                        -i "${AOWIS_BGFX_DIR}/src"
                        --varyingdef "${AOWIS_BGFX_SHADER_DIR}/${aowis_varyings}"
                        --type ${aowis_type}
                        --platform ${aowis_platform}
                        --profile ${aowis_profile}
                        ${aowis_optimize}
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${aowis_binary}" "${aowis_committed_dir}/${aowis_shader_name}.bin")
            endforeach()
            set(aowis_done "${aowis_out_dir}/inputs.sha256")
            list(APPEND aowis_commands
                COMMAND "${CMAKE_COMMAND}" -E copy "${aowis_stamp_source}" "${aowis_done}"
                COMMAND "${CMAKE_COMMAND}" -E copy "${aowis_stamp_source}" "${aowis_committed_stamp}")
            add_custom_command(OUTPUT ${aowis_family_binaries} "${aowis_done}"
                ${aowis_commands}
                DEPENDS ${AOWIS_BGFX_SHADER_INPUTS} "${aowis_stamp_source}" aowis-bgfx-shaderc
                COMMENT "Compiling bgfx ${aowis_family} shaders into src/map/bgfx/shaders/bin/${aowis_family}"
                VERBATIM)
            list(APPEND aowis_regeneration_stamps "${aowis_done}")
            if(NOT aowis_up_to_date)
                message(STATUS "bgfx ${aowis_family} shaders: out of date, regenerated by this build")
            endif()
        elseif(aowis_up_to_date)
            foreach(aowis_shader_name IN LISTS AOWIS_BGFX_SHADER_NAMES)
                list(APPEND aowis_family_binaries "${aowis_committed_dir}/${aowis_shader_name}.bin")
            endforeach()
        elseif(NOT aowis_required_index EQUAL -1)
            if(aowis_producible)
                message(FATAL_ERROR
                    "The committed bgfx ${aowis_family} shaders in "
                    "src/map/bgfx/shaders/bin/${aowis_family} do not match their sources "
                    "(a .sc file, a varying file or bgfx's bgfx_shader.sh changed). "
                    "Regenerate them with a native build configured with "
                    "-DAOWIS_BGFX_SHADER_TOOLS=ON (e.g. ./compile_linux.sh --shader-tools) "
                    "and commit src/map/bgfx/shaders/bin.")
            endif()
            message(WARNING
                "bgfx ${aowis_family} shaders are missing or out of date; they need "
                "Wine on this host (-DAOWIS_BGFX_SHADER_TOOLS=ON, AOWIS_BGFX_WINE). That "
                "renderer is not embedded; bgfx falls back to the next renderer with shaders.")
        endif()

        if(NOT aowis_required_index EQUAL -1 AND aowis_family_binaries)
            foreach(aowis_binary IN LISTS aowis_family_binaries)
                get_filename_component(aowis_alias "${aowis_binary}" NAME)
                set_source_files_properties("${aowis_binary}" PROPERTIES
                    QT_RESOURCE_ALIAS "${aowis_alias}")
            endforeach()
            qt_add_resources(${target} "aowis-bgfx-shaders-${aowis_family}"
                PREFIX "/aowis/map/bgfx/${aowis_family}"
                FILES ${aowis_family_binaries})
            list(APPEND aowis_embedded_families "${aowis_family}")
        endif()
    endforeach()

    if(NOT aowis_embedded_families)
        message(FATAL_ERROR "No bgfx shader family is available for ${AOWIS_BGFX_RENDERERS}")
    endif()
    message(STATUS "AOWIS bgfx shader families embedded: ${aowis_embedded_families}")
    if(aowis_regeneration_stamps)
        # rcc must not read the binaries before shaderc wrote them.
        add_custom_target(aowis-bgfx-shaders ALL DEPENDS ${aowis_regeneration_stamps})
        add_dependencies(${target} aowis-bgfx-shaders)
    endif()
endfunction()
