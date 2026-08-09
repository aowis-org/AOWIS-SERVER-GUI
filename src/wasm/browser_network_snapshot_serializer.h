#ifndef BROWSER_NETWORK_SNAPSHOT_SERIALIZER_H
#define BROWSER_NETWORK_SNAPSHOT_SERIALIZER_H

#include <QByteArray>
#include <QSet>
#include <QUuid>

class HydraulicData;
struct NetworkRenderSnapshot;
enum class VisualLink;
enum class VisualHeatmap;
enum class VisualNode;

namespace BrowserNetworkSnapshotSerializer
{
QByteArray serialize(const NetworkRenderSnapshot &snapshot);
QByteArray serializeGeometryPatch(const NetworkRenderSnapshot &snapshot,
                                  const QSet<QUuid> &node_uuids,
                                  const QSet<QUuid> &link_uuids);
QByteArray serializeSymbology(const HydraulicData &hydraulic_data,
                              VisualNode visual_node,
                              int node_size_percent,
                              int icon_size_percent,
                              VisualLink visual_link,
                              int link_thickness_px,
                              VisualHeatmap visual_heatmap,
                              int heatmap_opacity,
                              int heatmap_radius_m,
                              int heatmap_solid_center_percent);
}

#endif // BROWSER_NETWORK_SNAPSHOT_SERIALIZER_H
