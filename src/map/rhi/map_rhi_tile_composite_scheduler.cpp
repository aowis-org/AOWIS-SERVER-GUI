#include "map/rhi/map_rhi_tile_composite_scheduler.h"

#include <QMutexLocker>
#include <QPainter>
#include <QtGlobal>

#include <utility>

MapRhiTileCompositeResult buildTileCompositeResult(const MapRhiTileCompositeRequest &request)
{
    MapRhiTileCompositeResult result;
    result.request_id = request.request_id;
    result.imagery_key = request.imagery_key;
    result.source_key = request.source_key;

    if (request.use_children)
    {
        int child_width = 0;
        int child_height = 0;
        for (int dx = 0; dx < 2; ++dx)
        {
            for (int dy = 0; dy < 2; ++dy)
            {
                if (request.children[dx][dy].isNull())
                    return result;
                child_width = qMax(child_width, request.children[dx][dy].width());
                child_height = qMax(child_height, request.children[dx][dy].height());
            }
        }
        if (child_width <= 0 || child_height <= 0)
            return result;

        QImage composite(child_width * 2, child_height * 2, QImage::Format_RGBA8888);
        QPainter painter(&composite);
        for (int dx = 0; dx < 2; ++dx)
        {
            for (int dy = 0; dy < 2; ++dy)
            {
                // Same XYZ tile addressing used throughout (Y increasing
                // southward, matching image row order), so child (dx, dy)
                // maps directly onto quadrant (dx, dy) with no flip.
                painter.drawImage(
                    QRect(dx * child_width, dy * child_height, child_width, child_height),
                    request.children[dx][dy]);
            }
        }
        painter.end();
        result.image = composite;
        return result;
    }

    if (request.ancestor.isNull() || request.target_size.isEmpty()
        || request.ancestor_crop_rect.isEmpty())
    {
        return result;
    }

    result.image = request.ancestor.copy(request.ancestor_crop_rect)
        .scaled(request.target_size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    return result;
}

MapRhiTileCompositeScheduler::MapRhiTileCompositeScheduler()
{
    start();
}

MapRhiTileCompositeScheduler::~MapRhiTileCompositeScheduler()
{
    {
        QMutexLocker locker(&this->mutex);
        this->shutting_down = true;
    }
    this->wait_condition.wakeAll();
    // Blocks until run() returns. Any requests still queued at shutdown are
    // drained first (see run()), so this can take as long as finishing that
    // small backlog does; it is only ever hit while tearing the renderer
    // down, never on a frame-critical path.
    wait();
}

void MapRhiTileCompositeScheduler::submit(const MapRhiTileCompositeRequest &request)
{
    {
        QMutexLocker locker(&this->mutex);
        if (this->shutting_down)
            return;
        this->pending_requests.push_back(request);
    }
    this->wait_condition.wakeOne();
}

void MapRhiTileCompositeScheduler::collectReady(QVector<MapRhiTileCompositeResult> *results)
{
    if (results == nullptr)
        return;

    QMutexLocker locker(&this->mutex);
    if (this->completed_results.empty())
        return;

    results->reserve(results->size() + qsizetype(this->completed_results.size()));
    for (MapRhiTileCompositeResult &result : this->completed_results)
        results->append(std::move(result));
    this->completed_results.clear();
}

void MapRhiTileCompositeScheduler::run()
{
    for (;;)
    {
        MapRhiTileCompositeRequest request;
        {
            QMutexLocker locker(&this->mutex);
            while (this->pending_requests.empty() && !this->shutting_down)
                this->wait_condition.wait(&this->mutex);

            if (this->pending_requests.empty())
                return;

            request = std::move(this->pending_requests.front());
            this->pending_requests.pop_front();
        }

        MapRhiTileCompositeResult result = buildTileCompositeResult(request);

        {
            QMutexLocker locker(&this->mutex);
            this->completed_results.push_back(std::move(result));
        }
    }
}
