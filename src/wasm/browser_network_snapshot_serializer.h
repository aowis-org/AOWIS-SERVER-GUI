#ifndef BROWSER_NETWORK_SNAPSHOT_SERIALIZER_H
#define BROWSER_NETWORK_SNAPSHOT_SERIALIZER_H

#include <QByteArray>
#include <QSet>
#include <QUuid>

class HydraulicData;
struct NetworkRenderSnapshot;
struct NetworkSymbologyRanges;
struct NetworkSymbologySettings;

namespace BrowserNetworkSnapshotSerializer
{
QByteArray serialize(const NetworkRenderSnapshot &snapshot);
QByteArray serializeGeometryPatch(const NetworkRenderSnapshot &snapshot,
                                  const QSet<QUuid> &node_uuids,
                                  const QSet<QUuid> &link_uuids);
QByteArray serializeSymbology(const HydraulicData &hydraulic_data,
                              const NetworkSymbologySettings &settings,
                              const NetworkSymbologyRanges &ranges);
}

#endif // BROWSER_NETWORK_SNAPSHOT_SERIALIZER_H
