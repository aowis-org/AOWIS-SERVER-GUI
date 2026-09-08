#ifndef MAP_RHI_NETWORK_STYLE_H
#define MAP_RHI_NETWORK_STYLE_H

#include "map/rhi/map_rhi_symbology.h"
#include "network/network_render_snapshot.h"

#include <QHash>
#include <QImage>
#include <QSet>
#include <QUuid>

// Compact GPU-readable state shared by retained network renderers. Each
// entity occupies two RGBA8 texels addressed by a stable style index:
//
//   texel 0: base color (RGBA)
//   texel 1: R selected (0/255), G diagnostic (none/stale/error),
//            B visible (0/255), A flow direction (negative/zero/positive)
//
// Node style indices match their render IDs. Link indices follow the node
// range, ready for the compact-link/procedural-arrow phase.
class MapRhiNetworkStyleTable final
{
public:
    MapRhiNetworkStyleTable();

    void rebuild(
        const NetworkRenderSnapshot &snapshot,
        const QSet<QUuid> &hidden_entity_uuids,
        const MapRhiSymbology &symbology,
        InfrastructureEntity selected_entity_type,
        const QUuid &selected_entity_uuid,
        const QHash<QUuid, InfrastructureEntity> &simulation_error_entities,
        const QSet<QUuid> &simulation_stale_entity_uuids);

    const QImage &image() const;
    bool isValid() const;
    bool isDrawable(quint32 style_index) const;

    static quint32 nodeStyleIndex(quint32 render_id);
    quint32 linkStyleIndex(quint32 render_id) const;
    quint32 linkStyleBase() const;

private:
    void writeStyle(
        quint32 style_index, QRgb color, bool selected,
        int diagnostic_state, bool visible, int flow_direction);
    void writeTexel(
        quint64 texel_index, quint8 red, quint8 green,
        quint8 blue, quint8 alpha);
    const uchar *texel(quint64 texel_index) const;

    QImage image_data;
    quint32 link_style_base = 0;
};

#endif // MAP_RHI_NETWORK_STYLE_H
