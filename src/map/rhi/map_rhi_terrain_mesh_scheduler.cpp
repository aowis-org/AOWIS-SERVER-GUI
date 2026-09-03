#include "map/rhi/map_rhi_terrain_mesh_scheduler.h"

#include <QMutexLocker>

#include <utility>

MapRhiTerrainMeshScheduler::MapRhiTerrainMeshScheduler()
{
    start();
}

MapRhiTerrainMeshScheduler::~MapRhiTerrainMeshScheduler()
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

void MapRhiTerrainMeshScheduler::submit(const MapRhiTerrainMeshRequest &request)
{
    {
        QMutexLocker locker(&this->mutex);
        if (this->shutting_down)
            return;
        this->pending_requests.push_back(request);
    }
    this->wait_condition.wakeOne();
}

void MapRhiTerrainMeshScheduler::collectReady(QVector<MapRhiTerrainMeshResult> *results)
{
    if (results == nullptr)
        return;

    QMutexLocker locker(&this->mutex);
    if (this->completed_results.empty())
        return;

    results->reserve(results->size() + qsizetype(this->completed_results.size()));
    for (MapRhiTerrainMeshResult &result : this->completed_results)
        results->append(std::move(result));
    this->completed_results.clear();
}

void MapRhiTerrainMeshScheduler::run()
{
    for (;;)
    {
        MapRhiTerrainMeshRequest request;
        {
            QMutexLocker locker(&this->mutex);
            while (this->pending_requests.empty() && !this->shutting_down)
                this->wait_condition.wait(&this->mutex);

            if (this->pending_requests.empty())
                return;

            request = std::move(this->pending_requests.front());
            this->pending_requests.pop_front();
        }

        MapRhiTerrainMeshResult result = buildTerrainMeshResult(request);

        {
            QMutexLocker locker(&this->mutex);
            this->completed_results.push_back(std::move(result));
        }
    }
}
