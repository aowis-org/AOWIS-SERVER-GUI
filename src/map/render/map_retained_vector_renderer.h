#ifndef MAP_RETAINED_VECTOR_RENDERER_H
#define MAP_RETAINED_VECTOR_RENDERER_H

#include "map/render/map_vector_document.h"

#include <QImage>
#include <QSize>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

class MapRetainedVectorRenderer
{
public:
    struct HorizontalBand
    {
        int physical_top = 0;
        int physical_height = 0;
        qreal logical_top = 0.0;
        qreal logical_height = 0.0;
    };

    using DocumentBuilder = std::function<bool(
        int band_index, const HorizontalBand &band, MapVectorDocument &document)>;

    static int idealThreadCount();
    static std::vector<HorizontalBand> createHorizontalBands(
        const QSize &logical_size, qreal device_pixel_ratio,
        int maximum_workers = 0, int bands_per_worker = 2);
    static QImage renderHorizontalBands(
        const QSize &logical_size, qreal device_pixel_ratio,
        const std::vector<HorizontalBand> &bands,
        const std::shared_ptr<std::atomic_bool> &cancelled,
        const DocumentBuilder &document_builder,
        bool smooth_pixmap_transform = false,
        int maximum_workers = 0);
};

#endif // MAP_RETAINED_VECTOR_RENDERER_H
