#ifndef MAP_NETWORK_STRUCTS_H
#define MAP_NETWORK_STRUCTS_H

#include <QString>

#include "_enums_structs.h"

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


#endif // MAP_NETWORK_STRUCTS_H
