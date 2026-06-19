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

struct Wgs84Coordinate
{
    double lon = 0.0;
    double lat = 0.0;
};



#endif // _ENUMS_STRUCTS_H
