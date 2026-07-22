#ifndef MAP_MODELS_H
#define MAP_MODELS_H

#include <QString>
#include <QLabel>

#include "map_entity_marker_label.h"

#include "../_enums_structs.h"

#include <aowis/model/gis.h>

enum class MapEntityPlacementMode
{
    None,
    CreateNew,
    MoveExisting
};

enum class CurveType
{
    Pump,
    Efficiency,
    Volume,
    Headloss
};

struct CurvePoint
{
    double x = 0.0;
    double y = 0.0;
};

struct EntityCurve
{
    int id = -1;
    QString name;
    
    CurveType type = CurveType::Volume;
    
    QVector<CurvePoint> points;
};


/*
struct EntityTank
{
    QString name;
    int id = -1;
    
    CoordinateWGS84 coord_wgs84;
    
    // EPANET tank bottom elevation.
    // This SHOULD come from more precise surveying than normal GPS altitude.
    std::optional<double> elevation_m;
    
    // Water levels are measured above the tank bottom, not above sea level.
    std::optional<double> level_init_m;
    std::optional<double> level_min_m;
    std::optional<double> level_max_m;
    
    // Used for normal cylindrical tanks.
    // If a volume curve is used, EPANET still wants a non-zero diameter,
    // but the curve defines the real volume behavior.
    std::optional<double> diameter_m;
    
    // Tank volume at minimum water level.
    std::optional<double> volume_min_m3;
    
    // Optional EPANET volume curve reference.
    // Used for non-cylindrical tanks.
    std::optional<int> volume_curve_id;
    
    // If false: EPANET prevents additional inflow at max level.
    // If true: excess inflow is treated as overflow/spillage.
    bool overflow_allowed = false;
};
struct EntityTankMarker
{
    EntityTank entity_tank;
    MapEntityMarkerLabel *label = nullptr;
    QString path_pixmap;
    
    bool selected = false;
};
*/

struct InfrastructureEntityReference
{
    InfrastructureEntity type = InfrastructureEntity::Unknown;
    QUuid uuid;
};
struct MapEntityMarker
{
    InfrastructureEntityReference entity;
    
    CoordinateWGS84 coord_wgs84;
    
    QString symbol_id;
    QString path_pixmap;
    QPointer<MapEntityMarkerLabel> label;
    
    bool selected = false;
};
struct PipeGeometry
{
    InfrastructureEntityReference start_node;
    InfrastructureEntityReference end_node;
    QList<CoordinateWGS84> intermediate_vertices;
};

struct DeviceLinkGeometry
{
    InfrastructureEntityReference start_node;
    InfrastructureEntityReference end_node;
    CoordinateWGS84 center_coordinate;
};


#endif // MAP_MODELS_H
