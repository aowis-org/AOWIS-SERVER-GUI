#include "map/render/map_globe_surface_render_frame.h"

static_assert(sizeof(MapGlobeSurfaceVertex) == 20);
static_assert(sizeof(MapGlobeSurfaceWireframeVertex) == 12);

bool MapGlobeSurfaceRenderResources::isValid() const
{
    return this->window_vertices != nullptr
        && this->window_indices != nullptr
        && this->cap_vertices != nullptr
        && this->cap_indices != nullptr
        && this->wireframe_vertices != nullptr;
}

bool MapGlobeSurfaceRenderFrame::isValid() const
{
    return this->viewport_size.isValid() && this->resources.isValid();
}
