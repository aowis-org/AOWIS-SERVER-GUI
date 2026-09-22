#ifndef MAP_RENDER_SURFACE_H
#define MAP_RENDER_SURFACE_H

#include "common/_enums_structs.h"
#include "geo/geo_wgs84_ellipsoid.h"
#include "map/render/map_network_render_symbology.h"
#include "network/network_render_snapshot.h"
#include "network/network_symbology.h"

#include <aowis/model/gis.h>

#include <QHash>
#include <QPoint>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QUuid>

class MapTerrainRepository;
class MapTileRepository;

enum class MapUndergroundMode
{
    XRay,
    Hide,
    Solid
};

struct MapRenderHit
{
    quint32 render_id = 0;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    QUuid uuid;

    bool isValid() const
    {
        return this->render_id != 0
            && this->entity_type != InfrastructureEntity::Unknown;
    }
};

enum class MapGlobeSurfaceHitSource
{
    Invalid,
    TerrainMesh,
    EllipsoidFallback
};

struct MapGlobeSurfaceHit
{
    CoordinateWGS84 coordinate;
    GeoWgs84Ellipsoid::EcefPositionD ecef_position;
    double surface_height_m = 0.0;
    double distance_m = 0.0;
    MapGlobeSurfaceHitSource source = MapGlobeSurfaceHitSource::Invalid;

    bool isValid() const
    {
        return this->source != MapGlobeSurfaceHitSource::Invalid;
    }

    bool isTerrainMesh() const
    {
        return this->source == MapGlobeSurfaceHitSource::TerrainMesh;
    }
};

// Backend-neutral operational surface used by the map UI. Implementations
// own their QWidget/lifecycle/GPU details separately; this interface only
// exposes map-rendering semantics that every GPU backend needs to provide.
// Keeping QObject/QWidget out of this seam also avoids forcing backend
// implementations into one concrete widget or lifecycle model.
class MapRenderSurface
{
public:
    virtual ~MapRenderSurface() = default;

    virtual QString graphicsApiName() const = 0;
    virtual MapRenderHit hitTest(const QPointF &screen_position) const = 0;
    virtual bool terrainCoordinateAtScreen(
        const QPointF &screen_position, CoordinateWGS84 *coordinate,
        bool request_missing_tile = true) = 0;
    virtual bool globeSurfaceRayHitAtScreen(
        const QPointF &screen_position, MapGlobeSurfaceHit *hit) const = 0;
    virtual bool panGlobeByTerrainPixels(const QPoint &delta_pixels) = 0;

    virtual void setNetworkSnapshot(const NetworkRenderSnapshot &snapshot) = 0;
    virtual void setHiddenEntityUuids(const QSet<QUuid> &hidden_entity_uuids) = 0;
    virtual void setNodeDeclutteringEnabled(bool enabled) = 0;
    virtual void setNetworkScreenTranslation(const QPointF &translation_pixels) = 0;
    virtual void setSymbology(const MapNetworkRenderSymbology &symbology) = 0;
    virtual void setVisualControlSettings(
        const NetworkSymbologySettings &settings) = 0;
    virtual void setTileRepository(MapTileRepository *tile_repository) = 0;
    virtual void setTerrainRepository(MapTerrainRepository *terrain_repository) = 0;
    virtual void setBackgroundOpacity(int opacity) = 0;
    virtual void setSelectedEntity(
        InfrastructureEntity entity_type, const QUuid &uuid) = 0;
    virtual void setSimulationErrorEntities(
        const QHash<QUuid, InfrastructureEntity> &error_entities,
        const QSet<QUuid> &stale_entity_uuids) = 0;
    virtual void setGlobe3dIconsEnabled(bool enabled) = 0;
    virtual void setUndergroundMode(MapUndergroundMode mode) = 0;
    virtual MapUndergroundMode undergroundMode() const = 0;
    virtual void setTerrainWireframeVisible(bool visible) = 0;
    virtual void setMapTilesVisible(bool visible) = 0;
    virtual void globeTerrainMeshProgress(
        int *completed, int *total, bool *active) const = 0;
};

#endif // MAP_RENDER_SURFACE_H
