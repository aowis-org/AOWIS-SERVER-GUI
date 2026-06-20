#ifndef _ENUMS_STRUCTS_H
#define _ENUMS_STRUCTS_H

enum MapProvider
{
    ArcGISSat,
    OpenStreetMap,
    OpenTopoMap
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
};

struct CoordinateUTM
{
    double easting = 0.0;
    double northing = 0.0;
    
    int zone = 0;
    bool hemisphere_northern = true;
};



#endif // _ENUMS_STRUCTS_H
