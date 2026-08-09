#include "map_retained_vector_renderer.h"

#include <QPainter>
#include <QRunnable>
#include <QSemaphore>
#include <QThread>
#include <QThreadPool>
#include <QtMath>

#include <utility>

namespace
{
struct RetainedVectorWorkers
{
    RetainedVectorWorkers()
    {
        this->thread_count = qMax(1, QThread::idealThreadCount());
        this->pool.setMaxThreadCount(this->thread_count);
        this->pool.setExpiryTimeout(-1);
    }

    QThreadPool pool;
    int thread_count = 1;
};

RetainedVectorWorkers &retainedVectorWorkers()
{
    static RetainedVectorWorkers workers;
    return workers;
}
}

int MapRetainedVectorRenderer::idealThreadCount()
{
    return retainedVectorWorkers().thread_count;
}

std::vector<MapRetainedVectorRenderer::HorizontalBand>
MapRetainedVectorRenderer::createHorizontalBands(
    const QSize &logical_size, qreal device_pixel_ratio,
    int maximum_workers, int bands_per_worker)
{
    if (!logical_size.isValid())
        return {};

    const qreal bounded_device_pixel_ratio = qMax<qreal>(1.0, device_pixel_ratio);
    const int physical_height = qMax(1, qCeil(logical_size.height() * bounded_device_pixel_ratio));
    const int worker_count = qBound(
        1,
        maximum_workers > 0 ? maximum_workers : idealThreadCount(),
        idealThreadCount());
    const int band_count = qMin(
        qMax(1, worker_count * qMax(1, bands_per_worker)),
        physical_height);

    std::vector<HorizontalBand> bands(band_count);
    for (int band_index = 0; band_index < band_count; ++band_index)
    {
        HorizontalBand &band = bands[band_index];
        const int physical_bottom = (band_index + 1) * physical_height / band_count;
        band.physical_top = band_index * physical_height / band_count;
        band.physical_height = physical_bottom - band.physical_top;
        band.logical_top = band.physical_top / bounded_device_pixel_ratio;
        band.logical_height = band.physical_height / bounded_device_pixel_ratio;
    }
    return bands;
}

QImage MapRetainedVectorRenderer::renderHorizontalBands(
    const QSize &logical_size, qreal device_pixel_ratio,
    const std::vector<HorizontalBand> &bands,
    const std::shared_ptr<std::atomic_bool> &cancelled,
    const DocumentBuilder &document_builder,
    bool smooth_pixmap_transform,
    int maximum_workers)
{
    if (!logical_size.isValid() || bands.empty() || !document_builder)
        return QImage();
    if (cancelled && cancelled->load(std::memory_order_relaxed))
        return QImage();

    const qreal bounded_device_pixel_ratio = qMax<qreal>(1.0, device_pixel_ratio);
    const QSize physical_size(
        qMax(1, qCeil(logical_size.width() * bounded_device_pixel_ratio)),
        qMax(1, qCeil(logical_size.height() * bounded_device_pixel_ratio)));
    if (!physical_size.isValid())
        return QImage();

    RetainedVectorWorkers &workers = retainedVectorWorkers();
    const int requested_workers = maximum_workers > 0 ? maximum_workers : workers.thread_count;
    const int worker_count = qMin(
        qBound(1, requested_workers, workers.thread_count),
        int(bands.size()));

    std::vector<QImage> band_images(bands.size());
    std::atomic_int next_band_index{0};
    QSemaphore completed_workers;

    for (int worker_index = 0; worker_index < worker_count; ++worker_index)
    {
        QRunnable *runnable = QRunnable::create(
            [&bands, &band_images, &next_band_index, &completed_workers,
             &document_builder, cancelled, bounded_device_pixel_ratio,
             logical_width = logical_size.width(), smooth_pixmap_transform]
        {
            while (true)
            {
                if (cancelled && cancelled->load(std::memory_order_relaxed))
                    break;

                const int band_index = next_band_index.fetch_add(1, std::memory_order_relaxed);
                if (band_index >= int(bands.size()))
                    break;

                const HorizontalBand &band = bands.at(band_index);
                MapVectorDocument document;
                if (!document_builder(band_index, band, document))
                    continue;
                if (cancelled && cancelled->load(std::memory_order_relaxed))
                    break;

                QImage band_image(
                    QSize(qMax(1, qCeil(logical_width * bounded_device_pixel_ratio)),
                          band.physical_height),
                    QImage::Format_ARGB32_Premultiplied);
                if (band_image.isNull())
                    continue;
                band_image.setDevicePixelRatio(bounded_device_pixel_ratio);
                band_image.fill(Qt::transparent);

                QPainter painter(&band_image);
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth_pixmap_transform);
                document.paint(painter);
                painter.end();
                band_images[band_index] = std::move(band_image);
            }
            completed_workers.release();
        });
        workers.pool.start(runnable);
    }

    for (int worker_index = 0; worker_index < worker_count; ++worker_index)
        completed_workers.acquire();

    if (cancelled && cancelled->load(std::memory_order_relaxed))
        return QImage();

    QImage image(physical_size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return QImage();
    image.setDevicePixelRatio(bounded_device_pixel_ratio);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    for (int band_index = 0; band_index < int(bands.size()); ++band_index)
    {
        const QImage &band_image = band_images.at(band_index);
        if (band_image.isNull())
            continue;
        painter.drawImage(QPointF(0.0, bands.at(band_index).logical_top), band_image);
    }
    painter.end();
    return image;
}
