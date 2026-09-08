#ifndef MAP_RHI_LINK_MODEL_H
#define MAP_RHI_LINK_MODEL_H

#include <QVector>
#include <QtGlobal>

// One immutable unit quad is shared by every retained link segment. The
// vertex shader expands these along/side coordinates into a screen-space
// capsule using the per-instance endpoints below.
struct MapRhiLinkImpostorVertex
{
    float along = 0.0f;
    float side = 0.0f;
};

// Compact retained GPU data for one polyline segment. Cumulative distance
// and whole-link length let procedural flow arrows place repeated markers
// continuously across polyline joins without a second geometry stream.
struct MapRhiLinkInstance
{
    float start_x = 0.0f;
    float start_y = 0.0f;
    float start_z = 0.0f;
    float end_x = 0.0f;
    float end_y = 0.0f;
    float end_z = 0.0f;
    float cumulative_distance_world = 0.0f;
    // Absolute value is the complete parent-link length. A negative value
    // marks non-pipe link entities, whose legacy single-arrow position is
    // 30 percent instead of the pipe default of 50 percent.
    float total_length_world = 0.0f;
    // Kept as a float because the shader bundle includes legacy GLSL/ESSL
    // targets that do not support unsigned integer vertex attributes.
    float style_index = 0.0f;
    // CPU-side identity retained for picking and future chunk migration. It
    // is intentionally not exposed as a shader attribute in this phase.
    quint32 render_id = 0;
};

static_assert(sizeof(MapRhiLinkImpostorVertex) == 8,
              "Link impostor vertices must remain two floats");
static_assert(sizeof(MapRhiLinkInstance) == 40,
              "Link instances must remain compact");

const QVector<MapRhiLinkImpostorVertex> &mapRhiLinkImpostorVertices();

#endif // MAP_RHI_LINK_MODEL_H
