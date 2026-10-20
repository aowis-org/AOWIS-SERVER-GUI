#ifndef MAP_NETWORK_RENDER_SYMBOLOGY_H
#define MAP_NETWORK_RENDER_SYMBOLOGY_H

#include "network/network_symbology.h"

#include <QColor>
#include <QHash>
#include <QUuid>
#include <QVector>
#include <QtGlobal>

#include <limits>

class HydraulicData;
struct NetworkRenderSnapshot;

// Backend-neutral resolved network symbology: settings, ranges and the raw
// per-entity values. No colours: the renderer turns values into colours on
// the GPU (map_bgfx_entity_style.h), so a time step, a palette or a range
// change never walks the entities on the CPU to recolour them.
//
// Per-entity data is dense and indexed by render id (render ids are dense
// from 1 per entity class, NetworkRenderSnapshot; index 0 is unused). A
// value is NaN where the entity has none. The vectors are empty while their
// visual is None.
struct MapNetworkRenderSymbology
{
    NetworkSymbologySizeUnit node_size_unit = NetworkSymbologySizeUnit::Pixels;
    bool show_junctions = true;
    int node_size_px = NetworkSymbologyDefaultNodeSizePx;
    double node_size_m = NetworkSymbologyDefaultNodeSizeM;
    NetworkSymbologySizeUnit icon_size_unit = NetworkSymbologySizeUnit::Pixels;
    int icon_size_px = NetworkSymbologyDefaultIconSizePx;
    double icon_size_m = NetworkSymbologyDefaultIconSizeM;
    bool show_icons = true;
    QRgb icon_default_fill_color = qRgb(70, 70, 70);
    VisualNode visual_node = VisualNode::None;
    NetworkSymbologyPalette node_palette = NetworkSymbologyDefaultNodePalette;
    bool node_palette_flipped = false;
    VisualLink visual_link = VisualLink::None;
    NetworkSymbologyPalette link_palette = NetworkSymbologyDefaultLinkPalette;
    bool link_palette_flipped = false;
    NetworkSymbologySizeUnit link_thickness_unit = NetworkSymbologySizeUnit::Pixels;
    int link_thickness_px = NetworkSymbologyDefaultLinkThicknessPx;
    double link_thickness_m = NetworkSymbologyDefaultLinkThicknessM;
    bool show_flow_direction = true;
    int flow_direction_size_px = 10;
    VisualHeatmap visual_heatmap = VisualHeatmap::None;
    int heatmap_opacity = 75;
    HeatmapRadiusUnit heatmap_radius_unit = HeatmapRadiusUnit::Meters;
    int heatmap_radius_m = 400;
    int heatmap_radius_px = 50;
    int heatmap_solid_center_percent = 70;
    NetworkSymbologyPalette heatmap_palette = NetworkSymbologyPalette::Viridis;
    bool heatmap_palette_flipped = false;
    // Colour ranges of the three metrics (the legend's ramps).
    NetworkSymbologyRanges ranges;
    // Colours while a visual is None (the monitor: black; the editor: black
    // nodes and pipes, brown pumps and valves).
    QRgb node_base_color = qRgb(0, 0, 0);
    QRgb pipe_base_color = qRgb(0, 0, 0);
    QRgb device_link_base_color = qRgb(0, 0, 0);
    // Canonical values of the colouring metrics (also the map's value
    // labels, map_label_settings.h) and of the heatmap metric.
    QVector<double> node_values;
    QVector<double> link_values;
    QVector<double> heatmap_values;
    // Current flow direction per link (-1, 0, +1); empty while no hydraulic
    // result is shown or the flow arrows are off.
    QVector<qint8> flow_directions;

    // Whether the heatmap shows anything (the pass is skipped otherwise).
    bool heatmapActive() const
    {
        return this->visual_heatmap != VisualHeatmap::None && !this->heatmap_values.isEmpty()
            && this->heatmap_opacity > 0;
    }
};

// Value at a render id; NaN outside the vector (no value).
inline double mapNetworkSymbologyValue(const QVector<double> &values, quint32 render_id)
{
    return qsizetype(render_id) < values.size() ? values[qsizetype(render_id)]
                                                : std::numeric_limits<double>::quiet_NaN();
}

// Resolves the symbology inputs of the current time step into dense
// per-entity values. Keeps the uuid -> render id mapping of the snapshot and,
// per value source, the render ids in the order the source lists them: the
// result lists of every time step have the same order, so a step costs one
// uuid comparison per entity instead of a hash lookup (and never builds a
// hash table).
class MapNetworkSymbologyResolver
{
public:
    MapNetworkRenderSymbology resolve(const HydraulicData &hydraulic_data,
                                      const NetworkSymbologySettings &settings,
                                      const NetworkSymbologyRanges &ranges);

private:
    // Render ids of one visited source, by position in the visit.
    struct OrderCache
    {
        QVector<QUuid> uuids;
        QVector<quint32> render_ids;
    };
    enum Source
    {
        NodeValueSource,
        LinkValueSource,
        HeatmapValueSource,
        FlowSource,
        SourceCount
    };

    void synchronizeSnapshot(const NetworkRenderSnapshot &snapshot);
    // Render id of the entity visited at position index of a source (0: not
    // rendered). The cache entry is replaced when the order changed.
    quint32 renderIdAt(OrderCache *cache, qsizetype index, const QUuid &uuid,
                       const QHash<QUuid, quint32> &render_ids) const;
    // Visitor for the network_symbology_values.h sources: stores every
    // visited value at its render id in a dense vector (sized to the render
    // id range by the caller); finish() drops a stale tail of the order cache.
    struct ValueWriter
    {
        const MapNetworkSymbologyResolver *resolver = nullptr;
        OrderCache *cache = nullptr;
        const QHash<QUuid, quint32> *render_ids = nullptr;
        QVector<double> *values = nullptr;
        qsizetype index = 0;

        void operator()(const QUuid &uuid, double value);
        void finish();
    };
    ValueWriter valueWriter(Source source, const QHash<QUuid, quint32> &render_ids,
                            QVector<double> *values);

    quint64 snapshot_geometry_revision = 0;
    bool snapshot_known = false;
    qsizetype node_count = 0;
    qsizetype link_count = 0;
    QHash<QUuid, quint32> node_render_ids;
    QHash<QUuid, quint32> link_render_ids;
    OrderCache order_caches[SourceCount];
};

#endif // MAP_NETWORK_RENDER_SYMBOLOGY_H
