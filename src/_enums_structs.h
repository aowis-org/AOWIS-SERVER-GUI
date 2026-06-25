#ifndef _ENUMS_STRUCTS_H
#define _ENUMS_STRUCTS_H

enum MapServerMode
{
    REST,
    Standalone
};

enum MapProvider
{
    ArcGISSat       = 1,
    OpenTopoMap     = 2,
    OpenStreetMap   = 3,
    OSMCyclo        = 4
};

enum StatusColorCode
{
    None,
    Green,
    Yellow,
    Red
};

enum CanvasMode
{
    Edit,
    Monitor
};

struct CoordinateWGS84
{
    double lon = 0.0;
    double lat = 0.0;
    // GPS altitude, if available.
    // Not necessarily suitable as hydraulic EPANET elevation.
    std::optional<double> altitude_m;
};

struct CoordinateUTM
{
    double easting = 0.0;
    double northing = 0.0;
    
    int zone = 0;
    bool hemisphere_northern = true;
    
    bool isUPS() const
    {
        return zone == 0;
    }
    
    bool isUTM() const
    {
        return zone >= 1 && zone <= 60;
    }
};

// this is UTM coordinates but relative to a user set project origin in CoordinateUTM
// this helps to keep the numbers small for EPANET export
struct CoordinateLocal
{
    double x = 0.0; // meters east from project origin
    double y = 0.0; // meters north from project origin
};

enum MapEditTool
{
    Select          = 100,
    
    Pipe            = 1,
    Junction        = 2,
    Valve           = 3,
    Customer_Point  = 4,
    Pump            = 5,
    Tank            = 6,
    Power           = 7,
    Reservoir       = 8,
    Note            = 9
};
enum MapEditToolSub
{
    Tool_1 = 1,
    Tool_2 = 2,
    Tool_3 = 3,
    Tool_4 = 4,
    Tool_5 = 5
};

#endif // _ENUMS_STRUCTS_H
