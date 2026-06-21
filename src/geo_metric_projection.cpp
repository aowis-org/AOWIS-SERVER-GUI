#include "geo_metric_projection.h"

#include <GeographicLib/UTMUPS.hpp>
#include <GeographicLib/Geodesic.hpp>

GeoMetricProjection::GeoMetricProjection(QObject *parent)
    : QObject{parent}
{
    
}

