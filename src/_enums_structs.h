#ifndef _ENUMS_STRUCTS_H
#define _ENUMS_STRUCTS_H

enum MapServerMode
{
    REST,
    Standalone
};

enum MapProvider
{
    ArcGISSat,
    OpenStreetMap,
    OpenTopoMap,
    OSMCyclo
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
    double alt = 0.0;
    bool alt_exists = false;
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
    Select         = 1,
    Reservoir      = 2,
    Tank           = 3,
    Pump           = 4,
    Valve          = 5,
    Junction       = 6,
    Pipe           = 7,
    Customer_Point = 8,
    Power          = 9,
    Note           = 0,
};

#endif // _ENUMS_STRUCTS_H
