#ifndef MAP_RHI_FRUSTUM_H
#define MAP_RHI_FRUSTUM_H

#include <QMatrix4x4>
#include <QVector3D>

// A coarse, sphere-based view-frustum test used to skip uploading/drawing
// network geometry that cannot possibly be on screen this frame. Only the
// four side planes (left/right/top/bottom) are extracted; near/far are
// deliberately not tested -- see mapRhiExtractFrustumSidePlanes() for why.
// This mirrors the project's existing tile-culling philosophy (see
// globeQuadtreeNodeOccludedByHorizon() in map_rhi_globe_renderer.cpp): a
// cheap, safe-side "is this even worth drawing" test, not exact clipping.
// It can only ever produce a false positive (something off-screen still
// gets drawn), never a false negative that would make visible geometry
// disappear.
struct MapRhiFrustumPlane
{
    // The half-space "inside" this plane is normal . point + distance >= 0,
    // with (normal_x, normal_y, normal_z) unit length so that value is a
    // true signed distance in world units.
    float normal_x = 0.0f;
    float normal_y = 0.0f;
    float normal_z = 0.0f;
    float distance = 0.0f;
};

struct MapRhiFrustumSidePlanes
{
    // Order: left, right, bottom, top.
    MapRhiFrustumPlane planes[4];
};

// Extracts the four side planes of the view frustum implied by a
// world-to-clip-space matrix (any projection: perspective or orthographic,
// with or without an RHI clip-space correction baked in). The extraction is
// purely algebraic (Gribb/Hartmann) and does not depend on the near/far
// depth-range convention of the target graphics API, which is exactly why
// near/far planes are not extracted here: skipping them sidesteps that
// per-backend ambiguity entirely, at the cost of not culling anything
// strictly in front of the near plane or beyond the far plane (a
// comparatively small loss -- the side planes are what deliver the "this is
// off to the side of the screen" culling that matters for a large network).
MapRhiFrustumSidePlanes mapRhiExtractFrustumSidePlanes(const QMatrix4x4 &view_projection);

// Returns true if a world-space bounding sphere might be visible inside the
// frustum's four side planes.
bool mapRhiFrustumMayContainSphere(
    const MapRhiFrustumSidePlanes &frustum, const QVector3D &center_world, float radius_world);

#endif // MAP_RHI_FRUSTUM_H
