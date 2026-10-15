#ifndef MAP_RENDER_DIAGNOSTICS_H
#define MAP_RENDER_DIAGNOSTICS_H

// Opt-in, backend-neutral baseline counters for the bgfx migration.
// Enable AOWIS_MAP_RENDER_DIAGNOSTICS=1 before starting the application.
// A cumulative JSON snapshot is logged at most once every five seconds while
// Map Monitor frames or CPU cache requests are being processed. The snapshot
// can also be queried by benchmarks without requiring a GPU or a Qt window.
// Counters are approximate across concurrent workers, but none of them
// changes rendering behavior or owns rendering resources.

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>

namespace MapRenderDiagnostics
{
enum class Counter : size_t
{
    ZoomBoundary,
    FractionalZoom,
    CenterChange,
    Frame,
    TerrainBufferUpload,
    TerrainBufferBytes,
    CpuCacheForced,
    CpuCacheZoomRequest,
    CpuCacheSubmitted,
    CpuCacheCancelled,
    CpuCacheAccepted,
    CpuCacheRejected,
    CpuGeometryPrepared,
    CpuRasterPrepared,
    CpuOverlayPaint,
    DemandAttachmentsPrepared,
    FlatGeometryRebuilt,
    FlatIconsRebuilt,
    FlatFlowRebuilt,
    GlobeGeometryRebuilt,
    GlobeFlowRebuilt,
    GlobeOriginRebased,
    NetworkBufferUpload,
    NetworkBufferBytes,
    Count
};

inline constexpr std::array<const char *, static_cast<std::size_t>(Counter::Count)> CounterNames{{
    "zoom_boundary", "fractional_zoom", "center_change", "rhi_render_cpu_frames",
    "terrain_buffer_uploads", "terrain_buffer_bytes",
    "cpu_cache_forced", "cpu_cache_zoom_requests", "cpu_cache_submitted", "cpu_cache_cancelled",
    "cpu_cache_accepted", "cpu_cache_rejected", "cpu_geometry_preparation_calls",
    "cpu_raster_attempts", "cpu_overlay_paint", "demand_attachments_prepared",
    "flat_geometry_rebuilt", "flat_icons_rebuilt",
    "flat_flow_rebuilt", "globe_geometry_rebuilt", "globe_flow_rebuilt",
    "globe_origin_rebased", "network_buffer_uploads", "network_buffer_bytes"
}};

using Clock = std::chrono::steady_clock;

struct State
{
    std::array<std::atomic<quint64>, static_cast<std::size_t>(Counter::Count)> values{};
    std::array<std::atomic<quint64>, static_cast<std::size_t>(Counter::Count)> nanoseconds{};
    std::array<std::atomic<quint64>, static_cast<std::size_t>(Counter::Count)> max_nanoseconds{};
    Clock::time_point started = Clock::now();
    std::atomic<qint64> next_report_ms{5000};
};

inline State &state()
{
    static State instance;
    return instance;
}

inline bool enabled()
{
    static const bool is_enabled = []
    {
        const char *value = std::getenv("AOWIS_MAP_RENDER_DIAGNOSTICS");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return is_enabled;
}

inline void add(Counter counter, quint64 amount = 1)
{
    if (enabled())
        state().values[static_cast<size_t>(counter)].fetch_add(amount, std::memory_order_relaxed);
}

inline quint64 value(Counter counter)
{
    return state().values[static_cast<size_t>(counter)].load(std::memory_order_relaxed);
}

inline void terrainUpload(quint64 bytes)
{
    if (!enabled() || bytes == 0)
        return;
    add(Counter::TerrainBufferUpload);
    add(Counter::TerrainBufferBytes, bytes);
}

inline void networkUpload(quint64 bytes)
{
    if (!enabled() || bytes == 0)
        return;
    add(Counter::NetworkBufferUpload);
    add(Counter::NetworkBufferBytes, bytes);
}

class Scope final
{
public:
    explicit Scope(Counter counter) : counter_(counter), active_(enabled())
    {
        if (this->active_)
            this->started_ = Clock::now();
    }

    ~Scope()
    {
        if (!this->active_)
            return;
        const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - this->started_).count();
        State &counters = state();
        const std::size_t index = static_cast<size_t>(this->counter_);
        counters.values[index].fetch_add(1, std::memory_order_relaxed);
        const quint64 elapsed_ns = static_cast<quint64>(duration);
        counters.nanoseconds[index].fetch_add(elapsed_ns, std::memory_order_relaxed);
        quint64 previous_max = counters.max_nanoseconds[index].load(std::memory_order_relaxed);
        while (previous_max < elapsed_ns
            && !counters.max_nanoseconds[index].compare_exchange_weak(
                previous_max, elapsed_ns, std::memory_order_relaxed))
        {
        }
    }

    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    Counter counter_;
    bool active_ = false;
    Clock::time_point started_{};
};

inline QJsonObject snapshot()
{
    const State &counters = state();
    QJsonObject counts;
    QJsonObject durations_ms;
    QJsonObject maximum_ms;
    for (std::size_t index = 0; index < CounterNames.size(); ++index)
    {
        counts.insert(QString::fromLatin1(CounterNames[index]),
            static_cast<double>(counters.values[index].load(std::memory_order_relaxed)));
        const quint64 nanos = counters.nanoseconds[index].load(std::memory_order_relaxed);
        if (nanos != 0)
            durations_ms.insert(QString::fromLatin1(CounterNames[index]), double(nanos) / 1.0e6);
        const quint64 maximum = counters.max_nanoseconds[index].load(std::memory_order_relaxed);
        if (maximum != 0)
            maximum_ms.insert(QString::fromLatin1(CounterNames[index]), double(maximum) / 1.0e6);
    }
    return QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("aowis_map_render_diagnostics")},
        {QStringLiteral("elapsed_ms"), double(std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - counters.started).count())},
        {QStringLiteral("counts"), counts},
        {QStringLiteral("total_duration_ms"), durations_ms},
        {QStringLiteral("max_duration_ms"), maximum_ms}
    };
}

inline void reportIfDue()
{
    if (!enabled())
        return;
    State &counters = state();
    const qint64 elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - counters.started).count();
    qint64 deadline = counters.next_report_ms.load(std::memory_order_relaxed);
    if (elapsed_ms < deadline ||
        !counters.next_report_ms.compare_exchange_strong(
            deadline, elapsed_ms + 5000, std::memory_order_relaxed))
    {
        return;
    }
    qInfo().noquote() << QJsonDocument(snapshot()).toJson(QJsonDocument::Compact);
}
}

#endif // MAP_RENDER_DIAGNOSTICS_H
