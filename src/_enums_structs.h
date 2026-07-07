#ifndef _ENUMS_STRUCTS_H
#define _ENUMS_STRUCTS_H

#include <optional>
#include <cstdint>

#include <QObject>


enum MapServerMode
{
    REST,
    Standalone
};

enum MapProvider
{
    ArcGISSat       = 1,
    OpenStreetMap   = 2,
    OpenTopoMap     = 3,
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

enum class InfrastructureEntity : std::uint8_t
{
    Unknown = 0,
    
    // Hydraulic / EPANET-style node entities
    Junction,
    Reservoir,
    Tank,
    
    // Hydraulic / EPANET-style link entities
    Pipe,
    Pump,
    Valve,
    
    // AOWIS hydraulic extensions
    CustomerPoint,
    
    // Electric node/storage/generation entities
    ElectricJunction,
    Battery,
    Generator,
    SolarPanel,
    Inverter,
    Transformer,
    
    // Electric link/control entities
    Cable,
    Switch,
    Fuse,
    CircuitBreaker,
    
    // Non-simulation / UI entities
    Note
};

enum class JunctionPhysicalKind
{
    Physical,
    Virtual
};

enum class HeadlossFormula
{
    None           = 0x0,
    HazenWilliams  = 0x1,
    DarcyWeisbach  = 0x2,
    ChezyManning   = 0x4
};

Q_DECLARE_FLAGS(HeadlossFormulas, HeadlossFormula)
Q_DECLARE_OPERATORS_FOR_FLAGS(HeadlossFormulas)

Q_DECLARE_METATYPE(HeadlossFormula)
Q_DECLARE_METATYPE(HeadlossFormulas)

enum class ValveType
{
    PRV, // Pressure Reducing Valve
    PSV, // Pressure Sustaining Valve
    FCV, // Flow Control Valve
    PBV, // Pressure Breaker Valve
    TCV, // Throttle Control Valve
    GPV, // General Purpose Valve
    PCV  // Positional Control Valve, EPANET 2.3+
};

#endif // _ENUMS_STRUCTS_H
