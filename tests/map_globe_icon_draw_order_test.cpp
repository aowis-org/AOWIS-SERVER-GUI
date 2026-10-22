#include "map/render/map_globe_icon_draw_order.h"

#include <QVector>
#include <QVector3D>

#include <cstdio>

namespace
{
bool check(bool condition, const char *message)
{
    if (!condition)
        std::fprintf(stderr, "globe icon ordering: %s\n", message);
    return condition;
}

void appendIcon(QVector<MapNetworkIconVertex> *vertices, float x)
{
    for (int corner = 0; corner < 6; ++corner)
    {
        MapNetworkIconVertex vertex;
        vertex.center_x = x;
        vertices->append(vertex);
    }
}
}

int main()
{
    QVector<MapNetworkIconVertex> vertices;
    // Deliberately in nearest-first snapshot order.
    appendIcon(&vertices, 2.0f);
    appendIcon(&vertices, 8.0f);
    appendIcon(&vertices, 4.0f);
    const QVector<quint32> from_origin = mapGlobeIconDrawOrder(vertices, QVector3D(0.0f, 0.0f, 0.0f));
    if (!check(from_origin.size() == 18, "expected six indices per icon")
        || !check(from_origin.at(0) == 6 && from_origin.at(6) == 12 && from_origin.at(12) == 0,
                  "far symbols must draw before near ones"))
        return 1;
    for (int index = 0; index < from_origin.size(); ++index)
    {
        if (!check(from_origin.at(index) == (index < 6 ? 6u + quint32(index)
                                      : index < 12 ? 12u + quint32(index - 6)
                                                   : quint32(index - 12)),
                   "each six-vertex quad must keep its triangle order"))
            return 1;
    }
    const QVector<quint32> from_other_side = mapGlobeIconDrawOrder(vertices, QVector3D(10.0f, 0.0f, 0.0f));
    if (!check(from_other_side.at(0) == 0 && from_other_side.at(6) == 12
               && from_other_side.at(12) == 6,
               "eye movement must update icon overlap ordering"))
        return 1;
    QVector<MapNetworkIconVertex> invalid(vertices);
    invalid.removeLast();
    if (!check(mapGlobeIconDrawOrder(invalid, QVector3D()).isEmpty(),
               "incomplete icon quad must be rejected"))
        return 1;
    std::puts("globe icon draw order: camera-relative far-to-near indices passed");
    return 0;
}
