#ifndef MAP_MODELS_H
#define MAP_MODELS_H

#include <QList>
#include <QString>
#include <QUuid>
#include <QVector>

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
