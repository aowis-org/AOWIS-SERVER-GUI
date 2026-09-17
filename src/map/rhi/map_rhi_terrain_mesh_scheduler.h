#ifndef MAP_RHI_TERRAIN_MESH_SCHEDULER_H
#define MAP_RHI_TERRAIN_MESH_SCHEDULER_H

#include "map/render/map_terrain_mesh_scheduler.h"

// Transitional source-compatibility aliases. The terrain mesh data and
// scheduler are backend-neutral and implemented under map/render.
using MapRhiTerrainMeshVertex = MapTerrainMeshVertex;
using MapRhiTerrainMeshRequest = MapTerrainMeshRequest;
using MapRhiTerrainMeshResult = MapTerrainMeshResult;
using MapRhiTerrainMeshScheduler = MapTerrainMeshScheduler;

#endif // MAP_RHI_TERRAIN_MESH_SCHEDULER_H
