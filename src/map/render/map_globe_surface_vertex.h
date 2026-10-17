#ifndef MAP_GLOBE_SURFACE_VERTEX_H
#define MAP_GLOBE_SURFACE_VERTEX_H

// Vertex layouts of the prepared globe surface (MapGlobeSurfacePreparation).
// MapBgfxGlobeSurface uploads them as they are: its bgfx vertex layouts must
// match these fields and sizes.

// Position relative to the render origin, imagery texture coordinates.
struct MapGlobeSurfaceVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

// Wireframe line vertex, relative to the render origin.
struct MapGlobeSurfaceWireframeVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

static_assert(sizeof(MapGlobeSurfaceVertex) == 20);
static_assert(sizeof(MapGlobeSurfaceWireframeVertex) == 12);

#endif // MAP_GLOBE_SURFACE_VERTEX_H
