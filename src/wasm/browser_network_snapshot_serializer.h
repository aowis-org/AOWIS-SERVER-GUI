#ifndef BROWSER_NETWORK_SNAPSHOT_SERIALIZER_H
#define BROWSER_NETWORK_SNAPSHOT_SERIALIZER_H

#include <QByteArray>
#include <QSet>
#include <QUuid>

struct NetworkRenderSnapshot;

namespace BrowserNetworkSnapshotSerializer
{
QByteArray serialize(const NetworkRenderSnapshot &snapshot);
QByteArray serializeGeometryPatch(const NetworkRenderSnapshot &snapshot,
                                  const QSet<QUuid> &node_uuids,
                                  const QSet<QUuid> &link_uuids);
}

#endif // BROWSER_NETWORK_SNAPSHOT_SERIALIZER_H
