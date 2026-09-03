# Build the USEPA EPANET-MSX solver against AOWIS's existing EPANET target.
#
# The upstream EPANET-MSX project normally configures its bundled EPANET 2.2
# and command-line runner. AOWIS needs only the MSX solver library, and it must
# use the same EPANET implementation as the rest of the adapter.

if(NOT TARGET epanet2)
    message(FATAL_ERROR
        "EPANET-MSX requires the AOWIS epanet2 target to be created first."
    )
endif()

set(AOWIS_SERVER_EPANET_MSX_SOLVER_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../external/epanet-msx/src/solver"
)

file(GLOB AOWIS_SERVER_EPANET_MSX_SOURCES CONFIGURE_DEPENDS
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/*.c"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/*.h"
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/*.dat"
)

add_library(epanetmsx ${AOWIS_SERVER_EPANET_MSX_SOURCES})

target_include_directories(epanetmsx
    PUBLIC
        "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/include"
    PRIVATE
        "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}"
)

target_link_libraries(epanetmsx
    PUBLIC
        epanet2
)

if(NOT MSVC)
    target_link_libraries(epanetmsx PUBLIC m)
endif()

find_package(OpenMP COMPONENTS C)
if(OpenMP_C_FOUND)
    target_compile_definitions(epanetmsx PRIVATE USE_OPENMP)
    target_link_libraries(epanetmsx PUBLIC OpenMP::OpenMP_C)
endif()

# The vendored USEPA snapshot conditionally includes stdlib.h only on Apple,
# although msxout.c uses calloc() and free() on every platform. Force-include
# the standard header without modifying the upstream submodule.
set(AOWIS_SERVER_EPANET_MSX_OUT_SOURCE
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/msxout.c"
)
if(MSVC)
    set_property(SOURCE "${AOWIS_SERVER_EPANET_MSX_OUT_SOURCE}"
        APPEND PROPERTY COMPILE_OPTIONS "/FIstdlib.h")
else()
    set_property(SOURCE "${AOWIS_SERVER_EPANET_MSX_OUT_SOURCE}"
        APPEND PROPERTY COMPILE_OPTIONS "-include" "stdlib.h")
endif()

# EPANET and EPANET-MSX both define a stagnant-flow threshold named
# Q_STAGNANT with external linkage. Rename the MSX translation-unit symbol at
# compile time so static builds can link both libraries into one process.
set(AOWIS_SERVER_EPANET_MSX_QUAL_SOURCE
    "${AOWIS_SERVER_EPANET_MSX_SOLVER_DIR}/msxqual.c"
)
set_property(SOURCE "${AOWIS_SERVER_EPANET_MSX_QUAL_SOURCE}"
    APPEND PROPERTY COMPILE_DEFINITIONS "Q_STAGNANT=MSX_Q_STAGNANT")

if(WIN32 AND NOT BUILD_SHARED_LIBS)
    # epanetmsx.h assumes Windows consumers use a DLL unless MSXDLLEXPORT is
    # supplied. Static AOWIS builds still need the toolkit's stdcall ABI but
    # must not mark its symbols as dllimport.
    target_compile_definitions(epanetmsx PUBLIC "MSXDLLEXPORT=__stdcall")
endif()

set_target_properties(epanetmsx PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
