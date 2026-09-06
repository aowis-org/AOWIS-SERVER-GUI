#include "map/rhi/map_rhi_frustum.h"

#include <cmath>

namespace
{
// Builds one side plane from a linear combination of the clip-space W row
// (row 3) and the X or Y row (row 0 or row 1) of the view-projection
// matrix -- the standard Gribb/Hartmann extraction for the column-vector
// convention (clip = view_projection * world_point) used throughout this
// codebase's QMatrix4x4 math. combine_row selects X (0) or Y (1); sign
// selects which of the two planes that row pair produces.
MapRhiFrustumPlane makeSidePlane(const QMatrix4x4 &view_projection, int combine_row, float sign)
{
    MapRhiFrustumPlane plane;
    plane.normal_x = view_projection(3, 0) + sign * view_projection(combine_row, 0);
    plane.normal_y = view_projection(3, 1) + sign * view_projection(combine_row, 1);
    plane.normal_z = view_projection(3, 2) + sign * view_projection(combine_row, 2);
    plane.distance = view_projection(3, 3) + sign * view_projection(combine_row, 3);

    const float length = std::sqrt(
        plane.normal_x * plane.normal_x
        + plane.normal_y * plane.normal_y
        + plane.normal_z * plane.normal_z);
    // A near-zero row combination should never happen for a valid
    // projection matrix; guard it anyway so a pathological matrix (for
    // example during the very first frame, before the camera has synced
    // from the map model) cannot turn this into a divide-by-zero that
    // culls every instance.
    if (length > 1e-8f)
    {
        const float inverse_length = 1.0f / length;
        plane.normal_x *= inverse_length;
        plane.normal_y *= inverse_length;
        plane.normal_z *= inverse_length;
        plane.distance *= inverse_length;
    }
    return plane;
}
}

MapRhiFrustumSidePlanes mapRhiExtractFrustumSidePlanes(const QMatrix4x4 &view_projection)
{
    // Clip space bounds are -w <= x <= w and -w <= y <= w, i.e.
    // Left: x + w >= 0, Right: w - x >= 0, Bottom: y + w >= 0, Top: w - y >= 0.
    // Row 3 of view_projection is always the clip-space W row regardless of
    // whether the matrix also carries an RHI clip-space correction (that
    // correction only ever remaps the Z range and/or flips Y, neither of
    // which changes which rows combine to form these four side planes).
    MapRhiFrustumSidePlanes frustum;
    frustum.planes[0] = makeSidePlane(view_projection, 0, 1.0f);
    frustum.planes[1] = makeSidePlane(view_projection, 0, -1.0f);
    frustum.planes[2] = makeSidePlane(view_projection, 1, 1.0f);
    frustum.planes[3] = makeSidePlane(view_projection, 1, -1.0f);
    return frustum;
}

bool mapRhiFrustumMayContainSphere(
    const MapRhiFrustumSidePlanes &frustum, const QVector3D &center_world, float radius_world)
{
    for (int plane_index = 0; plane_index < 4; ++plane_index)
    {
        const MapRhiFrustumPlane &plane = frustum.planes[plane_index];
        const float signed_distance =
            plane.normal_x * center_world.x()
            + plane.normal_y * center_world.y()
            + plane.normal_z * center_world.z()
            + plane.distance;
        // Entirely outside just one plane is enough to reject the whole
        // sphere: a sphere visible in the frustum must be at least
        // partially inside every side plane.
        if (signed_distance < -radius_world)
            return false;
    }
    return true;
}
