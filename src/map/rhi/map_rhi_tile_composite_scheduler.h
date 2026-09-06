#ifndef MAP_RHI_TILE_COMPOSITE_SCHEDULER_H
#define MAP_RHI_TILE_COMPOSITE_SCHEDULER_H

#include <QImage>
#include <QMutex>
#include <QRect>
#include <QSize>
#include <QString>
#include <QThread>
#include <QVector>
#include <QWaitCondition>

#include <deque>

// Everything needed to derive one Globe tile's "still loading" placeholder
// image from already-loaded neighboring tiles, carried entirely by value so
// the background thread never touches a live, main-thread-owned object
// (MapTileRepository or any QRhi resource).
//
// QImage is safe to pass across threads and read/write concurrently once
// each side holds its own copy -- Qt's implicit sharing detaches (deep-
// copies) on write rather than mutating data another thread might still be
// reading. QPixmap is NOT thread-safe the same way (it can wrap platform-
// native, GUI-thread-affine resources depending on backend), which is why
// every field here is a QImage: the conversion from MapTileRepository's
// cached QPixmap happens on the calling (render) thread before a request is
// ever built -- see MapRhiGlobeRenderer::ensureProvisionalTileResource().
//
// Exactly one of "children" (zooming out: all four populated, composed into
// a 2x2 mosaic) or "ancestor" (zooming in: cropped to this tile's footprint
// and upscaled) is used per request -- see buildTileCompositeResult().
struct MapRhiTileCompositeRequest
{
    quint64 request_id = 0;
    // Which TileResource (keyed by imagery_key in
    // MapRhiGlobeRenderer::tile_resources) this result belongs to.
    QString imagery_key;
    // Mirrors MapRhiGlobeRenderer::TileResource::provisional_source_key --
    // either "children:<zoom>/<x>/<y>" or the ancestor tile's own imagery
    // key -- so the result, once applied, can be recognized on a later
    // frame as "already showing this" without redoing the work.
    QString source_key;
    bool use_children = false;

    // Populated when use_children is true: the four direct children,
    // indexed [dx][dy], already converted to RGBA8888.
    QImage children[2][2];

    // Populated when use_children is false: the nearest already-loaded
    // ancestor (RGBA8888), the sub-rectangle of it (in the ancestor's own
    // pixel space) corresponding to the requesting tile's footprint, and
    // the size to upscale that crop to.
    QImage ancestor;
    QRect ancestor_crop_rect;
    QSize target_size;
};

struct MapRhiTileCompositeResult
{
    quint64 request_id = 0;
    QString imagery_key;
    QString source_key;
    // Null if the request's source data turned out to be unusable (e.g. a
    // zero-size crop rect) -- the caller simply tries again next frame in
    // that case, exactly as if no fallback source had been found at all.
    QImage image;
};

// Pure function -- reads only its argument, touches no live object -- so it
// is safe to call from any thread. Defined in
// map_rhi_tile_composite_scheduler.cpp.
MapRhiTileCompositeResult buildTileCompositeResult(const MapRhiTileCompositeRequest &request);

// Runs buildTileCompositeResult() on a single dedicated background thread,
// mirroring MapRhiTerrainMeshScheduler exactly (same mutex/condition-
// variable queue in both directions, same single-producer-thread contract
// for submit()/collectReady(), same plain-queue-over-cross-thread-signals
// choice to keep the threading surface small enough to verify by
// inspection) so deriving placeholders for a batch of newly-revealed tiles
// -- e.g. right after a fast pan or zoom-out reveals many at once -- never
// blocks the render thread's frame budget with CPU image compositing.
//
// submit() and collectReady() are safe to call only from the thread that
// constructed this scheduler (the render/GUI thread, matching how
// MapRhiGlobeRenderer uses it) -- they are not meant to be called
// concurrently from multiple producer threads.
class MapRhiTileCompositeScheduler : public QThread
{
public:
    MapRhiTileCompositeScheduler();
    ~MapRhiTileCompositeScheduler() override;

    void submit(const MapRhiTileCompositeRequest &request);

    // Moves every result completed since the last call into *results.
    // Never blocks the caller on the worker thread's progress.
    void collectReady(QVector<MapRhiTileCompositeResult> *results);

protected:
    void run() override;

private:
    QMutex mutex;
    QWaitCondition wait_condition;
    std::deque<MapRhiTileCompositeRequest> pending_requests;
    std::deque<MapRhiTileCompositeResult> completed_results;
    bool shutting_down = false;
};

#endif // MAP_RHI_TILE_COMPOSITE_SCHEDULER_H
