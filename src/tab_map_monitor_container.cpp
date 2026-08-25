#include "tab_map_monitor_container.h"

#include "hydraulic_data.h"
#include "network_symbology_values.h"
#include "infrastructure_entity_traits.h"

#ifdef Q_OS_WASM
#include "wasm/browser_network_snapshot_serializer.h"
#endif

#ifndef Q_OS_WASM
#include "gui_configuration.h"
#include "map/map_network_overlay_widget.h"
#include "map/map_terrain_repository.h"
#if AOWIS_HAS_QRHI
#include "map/map_rhi_widget.h"
#include "map/map_rhi_hud_widget.h"
#include "map/map_monitor_hud_controls.h"
#include "map/map_rhi_symbology.h"
#endif
#endif

#include <QColor>
#include <QContextMenuEvent>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QSignalBlocker>

#include <cmath>
#include <functional>

namespace
{
class SymbologySlider final : public QSlider
{
public:
    SymbologySlider(int value_minimum, int value_maximum, int value_default, const QString &description, const QString &unit_suffix, QWidget *parent = nullptr)
        : QSlider(Qt::Horizontal, parent),
          value_default(value_default),
          description(description),
          unit_suffix(unit_suffix)
    {
        setRange(value_minimum, value_maximum);
        setValue(this->value_default);
        connect(this, &QSlider::valueChanged, this, [this]
        {
            updateToolTip();
        });
        updateToolTip();
    }

    void setConfiguration(int value_minimum, int value_maximum, int value_default,
                          int value, const QString &description, const QString &unit_suffix)
    {
        const QSignalBlocker blocker(this);
        this->value_default = value_default;
        this->description = description;
        this->unit_suffix = unit_suffix;
        setRange(value_minimum, value_maximum);
        setValue(value);
        updateToolTip();
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::RightButton)
        {
            setValue(this->value_default);
            event->accept();
            return;
        }

        QSlider::mousePressEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent *event) override
    {
        setValue(this->value_default);
        event->accept();
    }

private:
    void updateToolTip()
    {
        setToolTip(QStringLiteral("%1\nCurrent: %2%3\nRange: %4%3 to %5%3\nDefault: %6%3\nRight-click to reset.")
                       .arg(this->description)
                       .arg(value())
                       .arg(this->unit_suffix)
                       .arg(minimum())
                       .arg(maximum())
                       .arg(this->value_default));
    }

    int value_default;
    QString description;
    QString unit_suffix;
};


class FlowDirectionSizeSlider final : public QSlider
{
public:
    explicit FlowDirectionSizeSlider(QWidget *parent = nullptr)
        : QSlider(Qt::Horizontal, parent)
    {
        setRange(0, 19);
        setSingleStep(1);
        setPageStep(1);
        setValue(positionForArrowSize(10));
        connect(this, &QSlider::valueChanged, this, [this]
        {
            updateToolTip();
        });
        updateToolTip();
    }

    int arrowSizePx() const
    {
        if (value() == 0)
            return 0;
        return 5 + value();
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::RightButton)
        {
            setValue(positionForArrowSize(10));
            event->accept();
            return;
        }

        QSlider::mousePressEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent *event) override
    {
        setValue(positionForArrowSize(10));
        event->accept();
    }

private:
    static int positionForArrowSize(int size_px)
    {
        if (size_px <= 0)
            return 0;
        return qBound(1, size_px - 5, 19);
    }

    void updateToolTip()
    {
        const int size_px = arrowSizePx();
        const QString current = size_px == 0
            ? QStringLiteral("Off")
            : QStringLiteral("%1 px").arg(size_px);
        setToolTip(QStringLiteral(
            "Controls the flow-direction arrow size for the current simulation timestep.\n"
            "The far-left detent is Off; moving one step right snaps directly to the minimum useful size.\n"
            "Current: %1\nActive range: 6 px to 24 px\nDefault: 10 px\nRight-click to reset.")
            .arg(current));
    }
};
}

#ifdef Q_OS_WASM
#include <emscripten.h>

EM_JS(int, aowisBrowserNetworkSetSnapshot, (const char *json_data, int json_size),
{
    if (!window.aowisBrowserNetwork ||
        typeof window.aowisBrowserNetwork.setSnapshot !== "function")
        return 0;

    try
    {
        const snapshot = JSON.parse(UTF8ToString(json_data, json_size));
        window.aowisBrowserNetwork.setSnapshot(snapshot);
        return 1;
    }
    catch (error)
    {
        console.error("Could not transfer AOWIS network snapshot:", error);
        return 0;
    }
});

EM_JS(int, aowisBrowserNetworkSetSymbology, (const char *json_data, int json_size),
{
    if (!window.aowisBrowserNetwork ||
        typeof window.aowisBrowserNetwork.setSymbology !== "function")
    {
        console.error(
            "AOWIS browser network symbology API is unavailable. " +
            "The cached aowis-browser-network.js is outdated.");
        return 0;
    }

    try
    {
        const symbology = JSON.parse(UTF8ToString(json_data, json_size));
        window.aowisBrowserNetwork.setSymbology(symbology);
        return 1;
    }
    catch (error)
    {
        console.error("Could not transfer AOWIS network symbology:", error);
        return 0;
    }
});

EM_JS(double, aowisBrowserNetworkHitTest, (double x, double y),
{
    if (!window.aowisBrowserNetwork ||
        typeof window.aowisBrowserNetwork.hitTest !== "function")
        return 0;

    const hit = window.aowisBrowserNetwork.hitTest(x, y);
    if (!hit)
        return 0;

    const entityType = Number(hit.entityType) >>> 0;
    const renderId = Number(hit.renderId) >>> 0;
    if (entityType === 0 || renderId === 0)
        return 0;

    return entityType * 4294967296 + renderId;
});

EM_JS(void, aowisBrowserNetworkSetBackground, (int red, int green, int blue, int opacity),
{
    if (!window.aowisBrowserNetwork ||
        typeof window.aowisBrowserNetwork.setBackground !== "function")
        return;

    window.aowisBrowserNetwork.setBackground(red, green, blue, opacity);
});

EM_JS(void, aowisBrowserNetworkSetOwnerId, (int owner_id),
{
    if (!window.aowisBrowserNetwork ||
        typeof window.aowisBrowserNetwork.setOwnerId !== "function")
        return;

    window.aowisBrowserNetwork.setOwnerId(owner_id);
});

EM_JS(void, aowisBrowserNetworkSetSelectedEntity, (int entity_type, double render_id),
{
    if (!window.aowisBrowserNetwork ||
        typeof window.aowisBrowserNetwork.setSelectedEntity !== "function")
        return;

    window.aowisBrowserNetwork.setSelectedEntity(render_id, entity_type);
});

EM_JS(void, aowisBrowserNetworkSetErrorEntities, (const char *json_data, int json_size),
{
    if (!window.aowisBrowserNetwork ||
        typeof window.aowisBrowserNetwork.setErrorEntities !== "function")
        return;

    try {
        window.aowisBrowserNetwork.setErrorEntities(
            JSON.parse(UTF8ToString(json_data, json_size)));
    } catch (error) {
        console.error("AOWIS browser network error-entity update failed", error);
    }
});

#endif

MapMonitorContainer::MapMonitorContainer(MapModel *map_model, MapTileRepository *tile_repository,
                                         MapTerrainRepository *terrain_repository,
                                         HydraulicData *hydraulic_data, GpsProvider *gps, QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) ),
    gps( gps ),
    map_model( map_model ),
    tile_repository( tile_repository ),
    terrain_repository( terrain_repository ),
    hydraulic_data( hydraulic_data ),
    map_stack( new QWidget(this) ),
    map_stack_layout( new QStackedLayout(this->map_stack) ),
    map( new MapWidget(this->map_model, this->tile_repository, this->gps, this->map_stack) ),
#ifndef Q_OS_WASM
    desktop_network_overlay( new MapNetworkOverlayWidget(this->map_model, this->hydraulic_data, this->map_stack) ),
#endif
    map_menu( new MapMonitorMenuWidget(this->map, this) )
{
    setContentsMargins(0, 0, 0, 0);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->setSpacing(0);
    
    QScrollArea *scroll_controls = new QScrollArea(this);
    scroll_controls->setMinimumWidth(Sizes::SidebarLeftWidth);
    scroll_controls->setMaximumWidth(Sizes::SidebarLeftWidth);
    scroll_controls->setWidgetResizable(true);
    scroll_controls->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_controls->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_controls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    
    // unfortunately we need to fix widget width depending on scrollbar visible with some black magic
    connect(scroll_controls->verticalScrollBar(), &QScrollBar::rangeChanged, this, [scroll_controls]
    {
        QScrollBar *sb = scroll_controls->verticalScrollBar();
        bool should_show = sb->maximum() > sb->minimum();
        
        int width_base = Sizes::SidebarLeftWidth;
        int width_sb = sb->sizeHint().width();
        int w = should_show ? width_base + width_sb : width_base;
        
        scroll_controls->setMinimumWidth(w);
        scroll_controls->setMaximumWidth(w);
    });
    
    scroll_controls->setWidget(this->map_menu);

    this->map_stack_layout->setContentsMargins(0, 0, 0, 0);
    this->map_stack_layout->setSpacing(0);
    this->map_stack_layout->setStackingMode(QStackedLayout::StackAll);
    connect(this->map_menu->mapNavigationWidget(), &MapNavigationWidget::signalSlideOpacityChanged,
        this, &MapMonitorContainer::setNetworkBackgroundOpacity);
    connect(this->map_menu->mapNavigationWidget(), &MapNavigationWidget::signalIconSizeChanged, this,
        [this](int size_percent)
    {
        this->symbology_settings.icon_size_percent = size_percent;
        applySymbology();
    });
    this->map->installEventFilter(this);
#ifndef Q_OS_WASM
    this->map_stack->installEventFilter(this);
#endif
    this->map_stack_layout->addWidget(this->map);
#ifndef Q_OS_WASM
    this->map_stack_layout->addWidget(this->desktop_network_overlay);
    this->map_stack_layout->setCurrentWidget(this->desktop_network_overlay);
#if AOWIS_HAS_QRHI
    if (desktopMapRenderer() == DesktopMapRenderer::Rhi)
    {
        MapRhiWidget *rhi_surface =
            new MapRhiWidget(this->map_model, QStringLiteral("monitor"), this->map_stack);
        MapRhiHudWidget *rhi_hud =
            new MapRhiHudWidget(this->map_model, this->gps, this->map_stack);
        MapMonitorViewModeHudWidget *view_mode_hud =
            new MapMonitorViewModeHudWidget(this->map_model, this->map_stack);
        MapMonitorCompassHudWidget *compass_hud =
            new MapMonitorCompassHudWidget(this->map_model, this->map_stack);
        MapMonitorTiltHudWidget *tilt_hud =
            new MapMonitorTiltHudWidget(this->map_model, this->map_stack);
        MapMonitorCameraDistanceHudWidget *camera_distance_hud =
            new MapMonitorCameraDistanceHudWidget(this->map_model, this->map_stack);
        this->desktop_rhi_surface = rhi_surface;
        this->desktop_rhi_hud = rhi_hud;
        this->desktop_view_mode_hud = view_mode_hud;
        this->desktop_compass_hud = compass_hud;
        this->desktop_camera_distance_hud = camera_distance_hud;
        this->desktop_tilt_hud = tilt_hud;
        this->desktop_rhi_hud->hide();
        this->desktop_view_mode_hud->hide();
        this->desktop_compass_hud->hide();
        this->desktop_camera_distance_hud->hide();
        this->desktop_tilt_hud->hide();
        rhi_surface->setTileRepository(this->tile_repository);
        rhi_surface->setTerrainRepository(this->terrain_repository);
        rhi_surface->setBackgroundOpacity(this->network_background_opacity);
        rhi_surface->setNetworkSnapshot(this->hydraulic_data->networkRenderSnapshot());
        applyDesktopRhiSymbology();
        applyDesktopRhiHighlights();

        // Initialize and submit the first GPU frame as a tiny probe behind the working CPU map.
        // Only promote the RHI widget to the visible map surface after that frame succeeded.
        this->desktop_rhi_surface->setGeometry(0, 0, 1, 1);
        this->desktop_rhi_surface->lower();
        this->desktop_rhi_surface->show();
        this->desktop_network_overlay->raise();

        connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded,
                rhi_surface, [this, rhi_surface]
        {
            rhi_surface->setNetworkSnapshot(this->hydraulic_data->networkRenderSnapshot());
            applyDesktopRhiSymbology();
            applyDesktopRhiHighlights();
        });
        connect(this->hydraulic_data, &HydraulicData::signalNetworkGeometryChanged,
                rhi_surface, [this, rhi_surface](quint64)
        {
            rhi_surface->setNetworkSnapshot(this->hydraulic_data->networkRenderSnapshot());
            applyDesktopRhiSymbology();
            applyDesktopRhiHighlights();
        });
        connect(this->hydraulic_data, &HydraulicData::signalNodeChanged, this,
                [this](InfrastructureEntity, const QUuid &)
        {
            if (this->symbology_settings.visual_node != VisualNode::None
                || this->symbology_settings.visual_heatmap != VisualHeatmap::None)
            {
                applyDesktopRhiSymbology();
            }
            applyDesktopRhiHighlights();
        });
        connect(this->hydraulic_data, &HydraulicData::signalLinkChanged, this,
                [this](InfrastructureEntity, const QUuid &)
        {
            if (this->symbology_settings.visual_link != VisualLink::None)
                applyDesktopRhiSymbology();
            applyDesktopRhiHighlights();
        });
        connect(this->hydraulic_data, &HydraulicData::signalSimulationHeadlossFormulaChanged,
                this, [this]
        {
            if (this->symbology_settings.visual_link == VisualLink::Roughness)
                applyDesktopRhiSymbology();
        });
        connect(this->hydraulic_data, &HydraulicData::signalSimulationResultTimelineChanged,
                this, [this](bool)
        {
            applyDesktopRhiHighlights();
            if (this->symbology_settings.show_flow_direction
                || nodeVisualUsesHydraulicSimulationResult(this->symbology_settings.visual_node)
                || linkVisualUsesHydraulicSimulationResult(this->symbology_settings.visual_link)
                || heatmapVisualUsesHydraulicSimulationResult(
                    this->symbology_settings.visual_heatmap))
            {
                applyDesktopRhiSymbology();
            }
        });
        connect(this->hydraulic_data,
                &HydraulicData::signalWaterQualitySimulationResultTimelineChanged,
                this, [this](bool)
        {
            if (this->symbology_settings.visual_node == VisualNode::WaterAge
                || this->symbology_settings.visual_link == VisualLink::WaterAge
                || this->symbology_settings.visual_heatmap == VisualHeatmap::WaterAge)
            {
                applyDesktopRhiSymbology();
            }
        });
        connect(this->hydraulic_data, &HydraulicData::signalCurrentSimulationResultChanged,
                this, [this](int)
        {
            if (this->symbology_settings.show_flow_direction
                || nodeVisualUsesHydraulicSimulationResult(this->symbology_settings.visual_node)
                || linkVisualUsesHydraulicSimulationResult(this->symbology_settings.visual_link)
                || heatmapVisualUsesHydraulicSimulationResult(
                    this->symbology_settings.visual_heatmap)
                || this->symbology_settings.visual_node == VisualNode::WaterAge
                || this->symbology_settings.visual_link == VisualLink::WaterAge
                || this->symbology_settings.visual_heatmap == VisualHeatmap::WaterAge)
            {
                applyDesktopRhiSymbology();
            }
        });
        connect(this->hydraulic_data, &HydraulicData::signalSelectedTank, rhi_surface,
                [rhi_surface](const HydraulicNodeTank &tank)
        {
            rhi_surface->setSelectedEntity(InfrastructureEntity::Tank, tank.uuid);
        });
        connect(this->hydraulic_data, &HydraulicData::signalSelectedReservoir, rhi_surface,
                [rhi_surface](const HydraulicNodeReservoir &reservoir)
        {
            rhi_surface->setSelectedEntity(InfrastructureEntity::Reservoir, reservoir.uuid);
        });
        connect(this->hydraulic_data, &HydraulicData::signalSelectedJunction, rhi_surface,
                [rhi_surface](const HydraulicNodeJunction &junction)
        {
            rhi_surface->setSelectedEntity(InfrastructureEntity::Junction, junction.uuid);
        });
        connect(this->hydraulic_data, &HydraulicData::signalSelectedPipe, rhi_surface,
                [rhi_surface](const HydraulicLinkPipe &pipe)
        {
            rhi_surface->setSelectedEntity(InfrastructureEntity::Pipe, pipe.uuid);
        });
        connect(this->hydraulic_data, &HydraulicData::signalSelectedPump, rhi_surface,
                [rhi_surface](const HydraulicLinkPump &pump)
        {
            rhi_surface->setSelectedEntity(InfrastructureEntity::Pump, pump.uuid);
        });
        connect(this->hydraulic_data, &HydraulicData::signalSelectedValve, rhi_surface,
                [rhi_surface](const HydraulicLinkValve &valve)
        {
            rhi_surface->setSelectedEntity(InfrastructureEntity::Valve, valve.uuid);
        });
        connect(rhi_surface, &MapRhiWidget::signalRendererReady, this,
                [this, rhi_surface, rhi_hud, view_mode_hud]
        {
            this->map_stack_layout->addWidget(rhi_surface);
            this->map_stack_layout->setCurrentWidget(rhi_surface);
            this->map->setRhiViewActive(true);
            this->desktop_network_overlay->hide();
            rhi_surface->show();

            // The HUD widgets are plain children of map_stack, not members of the
            // stacked layout. This leaves the map surface itself free to receive
            // mouse input everywhere outside the compact interactive controls.
            rhi_hud->show();
            view_mode_hud->show();
            positionDesktopHudWidgets();
            syncDesktopCameraHudVisibility();

            qInfo() << "Monitor map renderer: RHI map active with GPU basemap/heatmap and QWidget HUD; CPU renderer retained as fallback.";
        });
        connect(rhi_surface, &MapRhiWidget::signalRendererFailed, this,
                [this](const QString &reason)
        {
            qWarning().noquote()
                << QStringLiteral("Monitor RHI surface failed (%1). "
                                  "Falling back to the existing CPU renderer.")
                       .arg(reason);
            if (this->map_model->viewMode() != MapViewMode::TwoD)
                this->map_model->setViewMode(MapViewMode::TwoD);
            this->map->setRhiViewActive(false);
            this->desktop_network_overlay->show();
            this->map_stack_layout->setCurrentWidget(this->desktop_network_overlay);
            if (this->desktop_rhi_hud != nullptr)
                this->desktop_rhi_hud->hide();
            if (this->desktop_view_mode_hud != nullptr)
                this->desktop_view_mode_hud->hide();
            if (this->desktop_compass_hud != nullptr)
                this->desktop_compass_hud->hide();
            if (this->desktop_tilt_hud != nullptr)
                this->desktop_tilt_hud->hide();
            if (this->desktop_rhi_surface != nullptr)
                this->desktop_rhi_surface->hide();
        });
        connect(this->map_model, &MapModel::viewModeChanged, this,
                [this](MapViewMode)
        {
            syncDesktopCameraHudVisibility();
        });
    }
#endif
#endif

#ifdef Q_OS_WASM
    this->map->setBrowserMapLayerEnabled(true);
    this->map->setBrowserMapLayerTopmost(true);
    aowisBrowserNetworkSetOwnerId(this->map->browserMapLayerOwnerId());

    this->wasm_map_layer_sync_timer = new QTimer(this);
    this->wasm_map_layer_sync_timer->setSingleShot(true);
    connect(this->wasm_map_layer_sync_timer, &QTimer::timeout, this, &MapMonitorContainer::syncWasmMapLayer);

    this->wasm_network_symbology_sync_timer = new QTimer(this);
    this->wasm_network_symbology_sync_timer->setSingleShot(true);
    connect(this->wasm_network_symbology_sync_timer, &QTimer::timeout, this, &MapMonitorContainer::syncWasmNetworkSymbology);

    this->installEventFilter(this);
    this->map_stack->installEventFilter(this);
    if (this->window())
        this->window()->installEventFilter(this);

    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this, [this]
    {
        scheduleWasmMapLayerSync();
        scheduleWasmNetworkSymbologySync();
    });
    connect(this->hydraulic_data, &HydraulicData::signalNetworkGeometryChanged, this, [this](quint64)
    {
        scheduleWasmMapLayerSync();
        scheduleWasmNetworkSymbologySync();
    });
    connect(this->hydraulic_data, &HydraulicData::signalNodeChanged, this,
        [this](InfrastructureEntity, const QUuid &)
    {
        scheduleWasmNetworkSymbologySync();
    });
    connect(this->hydraulic_data, &HydraulicData::signalLinkChanged, this,
        [this](InfrastructureEntity, const QUuid &)
    {
        scheduleWasmNetworkSymbologySync();
    });
    connect(this->hydraulic_data, &HydraulicData::signalSimulationHeadlossFormulaChanged,
        this, [this]
    {
        if (this->symbology_settings.visual_link == VisualLink::Roughness)
            scheduleWasmNetworkSymbologySync();
    });
    connect(this->hydraulic_data, &HydraulicData::signalSimulationResultTimelineChanged, this,
        [this](bool)
    {
        syncWasmSimulationErrorEntities();
        const bool hydraulic_symbology_active =
            nodeVisualUsesHydraulicSimulationResult(this->symbology_settings.visual_node)
            || linkVisualUsesHydraulicSimulationResult(this->symbology_settings.visual_link)
            || heatmapVisualUsesHydraulicSimulationResult(this->symbology_settings.visual_heatmap);
        if (this->symbology_settings.show_flow_direction || hydraulic_symbology_active)
            scheduleWasmNetworkSymbologySync();
    });
    connect(this->hydraulic_data, &HydraulicData::signalWaterQualitySimulationResultTimelineChanged,
        this, [this](bool)
    {
        if (this->symbology_settings.visual_node == VisualNode::WaterAge
            || this->symbology_settings.visual_link == VisualLink::WaterAge
            || this->symbology_settings.visual_heatmap == VisualHeatmap::WaterAge)
        {
            scheduleWasmNetworkSymbologySync();
        }
    });
    connect(this->hydraulic_data, &HydraulicData::signalCurrentSimulationResultChanged,
        this, [this](int)
    {
        const bool hydraulic_symbology_active =
            nodeVisualUsesHydraulicSimulationResult(this->symbology_settings.visual_node)
            || linkVisualUsesHydraulicSimulationResult(this->symbology_settings.visual_link)
            || heatmapVisualUsesHydraulicSimulationResult(this->symbology_settings.visual_heatmap);
        if (this->symbology_settings.show_flow_direction
            || hydraulic_symbology_active
            || this->symbology_settings.visual_node == VisualNode::WaterAge
            || this->symbology_settings.visual_link == VisualLink::WaterAge
            || this->symbology_settings.visual_heatmap == VisualHeatmap::WaterAge)
        {
            scheduleWasmNetworkSymbologySync();
        }
    });
    connect(this->hydraulic_data, &HydraulicData::signalSelectedTank, this,
        [this](const HydraulicNodeTank &tank)
    {
        syncWasmSelectedEntity(InfrastructureEntity::Tank, tank.uuid);
    });
    connect(this->hydraulic_data, &HydraulicData::signalSelectedReservoir, this,
        [this](const HydraulicNodeReservoir &reservoir)
    {
        syncWasmSelectedEntity(InfrastructureEntity::Reservoir, reservoir.uuid);
    });
    connect(this->hydraulic_data, &HydraulicData::signalSelectedJunction, this,
        [this](const HydraulicNodeJunction &junction)
    {
        syncWasmSelectedEntity(InfrastructureEntity::Junction, junction.uuid);
    });
    connect(this->hydraulic_data, &HydraulicData::signalSelectedPipe, this,
        [this](const HydraulicLinkPipe &pipe)
    {
        syncWasmSelectedEntity(InfrastructureEntity::Pipe, pipe.uuid);
    });
    connect(this->hydraulic_data, &HydraulicData::signalSelectedPump, this,
        [this](const HydraulicLinkPump &pump)
    {
        syncWasmSelectedEntity(InfrastructureEntity::Pump, pump.uuid);
    });
    connect(this->hydraulic_data, &HydraulicData::signalSelectedValve, this,
        [this](const HydraulicLinkValve &valve)
    {
        syncWasmSelectedEntity(InfrastructureEntity::Valve, valve.uuid);
    });

    this->syncWasmNetworkBackground();
    this->scheduleWasmMapLayerSync();
#endif

    this->layout->addWidget(scroll_controls);
    this->layout->addWidget(this->map_stack);
    
    connect(this->map_menu, &MapMonitorMenuWidget::signalNodeVisualClicked, this,
        [this](VisualNode visual_node)
    {
        this->symbology_settings.visual_node = visual_node;
        emit signalShowMapLegendNode(visual_node);
        applySymbology();
    });
    connect(this->map_menu, &MapMonitorMenuWidget::signalNodeSizeChanged, this, [this](int size_percent)
    {
        this->symbology_settings.node_size_percent = size_percent;
        applySymbology();
    });
    connect(this->map_menu, &MapMonitorMenuWidget::signalLinkVisualClicked, this,
        [this](VisualLink visual_link)
    {
        this->symbology_settings.visual_link = visual_link;
        emit signalShowMapLegendLink(visual_link);
        applySymbology();
    });
    connect(this->map_menu, &MapMonitorMenuWidget::signalLinkThicknessChanged, this, [this](int thickness_px)
    {
        this->symbology_settings.link_thickness_px = thickness_px;
        applySymbology();
    });
    connect(this->map_menu, &MapMonitorMenuWidget::signalFlowDirectionSizeChanged, this, [this](int size_px)
    {
        this->symbology_settings.flow_direction_size_px = size_px;
        this->symbology_settings.show_flow_direction = size_px > 0;
        applySymbology();
    });
    connect(this->map_menu, &MapMonitorMenuWidget::signalHeatmapVisualClicked, this, [this](VisualHeatmap visual_heatmap)
    {
        this->symbology_settings.visual_heatmap = visual_heatmap;
        emit signalShowMapLegendHeatmap(visual_heatmap);
        applySymbology();
    });
    connect(this->map_menu, &MapMonitorMenuWidget::signalHeatmapOpacityChanged, this, [this](int opacity)
    {
        this->symbology_settings.heatmap_opacity = opacity;
        applySymbology();
    });
    connect(this->map_menu, &MapMonitorMenuWidget::signalHeatmapRadiusUnitChanged, this,
        [this](HeatmapRadiusUnit unit)
    {
        this->symbology_settings.heatmap_radius_unit = unit;
        applySymbology();
    });
    connect(this->map_menu, &MapMonitorMenuWidget::signalHeatmapRadiusChanged, this,
        [this](HeatmapRadiusUnit unit, int radius)
    {
        if (unit == HeatmapRadiusUnit::Pixels)
            this->symbology_settings.heatmap_radius_px = radius;
        else
            this->symbology_settings.heatmap_radius_m = radius;
        applySymbology();
    });
    connect(this->map_menu, &MapMonitorMenuWidget::signalHeatmapSolidCenterChanged, this, [this](int percent)
    {
        this->symbology_settings.heatmap_solid_center_percent = percent;
        applySymbology();
    });
}

MapMonitorContainer::~MapMonitorContainer()
{
#ifdef Q_OS_WASM
    this->map->setBrowserMapLayerGeometry(QRect(), false);
#endif
}

#ifndef Q_OS_WASM
#if AOWIS_HAS_QRHI
void MapMonitorContainer::positionDesktopHudWidgets()
{
    constexpr int hud_margin_px = 12;

    if (this->desktop_rhi_hud != nullptr)
    {
        this->desktop_rhi_hud->setGeometry(this->map_stack->rect());
        if (this->desktop_rhi_hud->isVisible())
            this->desktop_rhi_hud->raise();
    }

    if (this->desktop_view_mode_hud != nullptr)
    {
        this->desktop_view_mode_hud->adjustSize();
        this->desktop_view_mode_hud->move(hud_margin_px, hud_margin_px);
        if (this->desktop_view_mode_hud->isVisible())
            this->desktop_view_mode_hud->raise();
    }

    if (this->desktop_tilt_hud != nullptr)
    {
        this->desktop_tilt_hud->adjustSize();
        const int tilt_y = qMax(
            hud_margin_px,
            this->map_stack->height() - this->desktop_tilt_hud->height() - hud_margin_px);
        this->desktop_tilt_hud->move(hud_margin_px, tilt_y);
        if (this->desktop_tilt_hud->isVisible())
            this->desktop_tilt_hud->raise();
    }

    if (this->desktop_camera_distance_hud != nullptr)
    {
        this->desktop_camera_distance_hud->adjustSize();
        const int distance_x = hud_margin_px
            + (this->desktop_tilt_hud != nullptr ? this->desktop_tilt_hud->width() + 8 : 0);
        const int distance_y = qMax(
            hud_margin_px,
            this->map_stack->height() - this->desktop_camera_distance_hud->height() - hud_margin_px);
        this->desktop_camera_distance_hud->move(distance_x, distance_y);
        if (this->desktop_camera_distance_hud->isVisible())
            this->desktop_camera_distance_hud->raise();
    }

    if (this->desktop_compass_hud != nullptr)
    {
        this->desktop_compass_hud->adjustSize();
        const int compass_x = hud_margin_px
            + (this->desktop_tilt_hud != nullptr ? this->desktop_tilt_hud->width() + 8 : 0)
            + (this->desktop_camera_distance_hud != nullptr
                ? this->desktop_camera_distance_hud->width() + 8 : 0);
        const int compass_y = qMax(
            hud_margin_px,
            this->map_stack->height() - this->desktop_compass_hud->height() - hud_margin_px);
        this->desktop_compass_hud->move(compass_x, compass_y);
        if (this->desktop_compass_hud->isVisible())
            this->desktop_compass_hud->raise();
    }
}

void MapMonitorContainer::syncDesktopCameraHudVisibility()
{
    if (this->desktop_compass_hud == nullptr || this->desktop_tilt_hud == nullptr
        || this->desktop_camera_distance_hud == nullptr)
    {
        return;
    }

    const bool rhi_active = this->desktop_rhi_surface != nullptr
        && this->desktop_rhi_surface->isVisible()
        && this->desktop_view_mode_hud != nullptr
        && this->desktop_view_mode_hud->isVisible();
    const bool camera_hud_visible =
        rhi_active && this->map_model->viewMode() == MapViewMode::ThreeD;
    this->desktop_compass_hud->setVisible(camera_hud_visible);
    this->desktop_camera_distance_hud->setVisible(camera_hud_visible);
    this->desktop_tilt_hud->setVisible(camera_hud_visible);
    if (camera_hud_visible)
    {
        this->desktop_compass_hud->raise();
        this->desktop_camera_distance_hud->raise();
        this->desktop_tilt_hud->raise();
        positionDesktopHudWidgets();
    }
}
#endif
#endif

bool MapMonitorContainer::eventFilter(QObject *watched, QEvent *event)
{
#ifndef Q_OS_WASM
#if AOWIS_HAS_QRHI
    if (watched == this->map_stack && event->type() == QEvent::Resize
        && this->desktop_rhi_hud != nullptr)
    {
        positionDesktopHudWidgets();
    }
#endif
#endif

    if (watched == this->map)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            QMouseEvent *mouse_event = static_cast<QMouseEvent *>(event);
            if (mouse_event->button() == Qt::LeftButton)
            {
#ifdef Q_OS_WASM
                if (selectWasmNetworkEntityAt(mouse_event->position()))
                {
                    mouse_event->accept();
                    return true;
                }
#else
#if AOWIS_HAS_QRHI
                if (this->desktop_rhi_surface != nullptr
                    && this->desktop_rhi_surface->isVisible())
                {
                    const MapRhiHit hit = this->desktop_rhi_surface->hitTest(
                        mouse_event->position());
                    if (hit.isValid()
                        && selectNetworkEntity(hit.render_id, hit.entity_type, hit.uuid))
                    {
                        this->desktop_rhi_surface->setSelectedEntity(hit.entity_type, hit.uuid);
                        mouse_event->accept();
                        return true;
                    }
                    this->desktop_rhi_surface->setSelectedEntity(
                        InfrastructureEntity::Unknown, QUuid());
                    this->desktop_network_overlay->clearSelectedEntity();
                }
                else
#endif
                {
                    const NetworkOverlayHit hit =
                        this->desktop_network_overlay->hitTest(mouse_event->position());
                    if (hit.isValid()
                        && selectNetworkEntity(hit.render_id, hit.entity_type, hit.uuid))
                    {
                        this->desktop_network_overlay->setSelectedEntity(hit);
                        mouse_event->accept();
                        return true;
                    }
                    this->desktop_network_overlay->clearSelectedEntity();
#if AOWIS_HAS_QRHI
                    if (this->desktop_rhi_surface != nullptr)
                        this->desktop_rhi_surface->setSelectedEntity(
                            InfrastructureEntity::Unknown, QUuid());
#endif
                }
#endif
            }
        }
#ifndef Q_OS_WASM
        else if (event->type() == QEvent::MouseMove)
        {
            QMouseEvent *mouse_event = static_cast<QMouseEvent *>(event);
            updateDesktopNetworkHover(mouse_event->position(), mouse_event->buttons());
        }
        else if (event->type() == QEvent::Leave || event->type() == QEvent::Hide)
        {
            setDesktopNetworkHovered(false);
        }
#endif
    }

#ifdef Q_OS_WASM
    if (watched == this || watched == this->map || watched == this->map_stack || watched == this->window())
    {
        switch (event->type())
        {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::LayoutRequest:
        case QEvent::ParentChange:
        case QEvent::WindowStateChange:
            scheduleWasmMapLayerSync();
            break;
        case QEvent::ApplicationPaletteChange:
        case QEvent::PaletteChange:
        case QEvent::StyleChange:
            this->syncWasmNetworkBackground();
            break;
        default:
            break;
        }
    }
#endif

    return QWidget::eventFilter(watched, event);
}

bool MapMonitorContainer::selectNetworkEntity(quint32 render_id, InfrastructureEntity entity_type, const QUuid &uuid)
{
    if (this->hydraulic_data == nullptr || render_id == 0 || entity_type == InfrastructureEntity::Unknown)
        return false;

    if (!uuid.isNull())
    {
        this->hydraulic_data->setSelectedUuid(entity_type, uuid);
        return true;
    }

    const NetworkRenderSnapshot &snapshot = this->hydraulic_data->networkRenderSnapshot();
    if (InfrastructureEntityTraits::isHydraulicConnectionNode(entity_type))
    {
        for (const NetworkRenderNode &node : snapshot.nodes)
        {
            if (node.render_id != render_id || node.entity_type != entity_type)
                continue;

            this->hydraulic_data->setSelectedUuid(entity_type, node.uuid);
            return true;
        }
        return false;
    }

    if (InfrastructureEntityTraits::isHydraulicNetworkLink(entity_type))
    {
        for (const NetworkRenderLink &link : snapshot.links)
        {
            if (link.render_id != render_id || link.entity_type != entity_type)
                continue;

            this->hydraulic_data->setSelectedUuid(entity_type, link.uuid);
            return true;
        }
    }

    return false;
}

#ifndef Q_OS_WASM
void MapMonitorContainer::updateDesktopNetworkHover(const QPointF &position, Qt::MouseButtons buttons)
{
    if (buttons != Qt::NoButton)
    {
        setDesktopNetworkHovered(false);
        return;
    }

#if AOWIS_HAS_QRHI
    if (this->desktop_rhi_surface != nullptr
        && this->desktop_rhi_surface->isVisible())
    {
        setDesktopNetworkHovered(this->desktop_rhi_surface->hitTest(position).isValid());
        return;
    }
#endif

    setDesktopNetworkHovered(this->desktop_network_overlay->hitTest(position).isValid());
}

void MapMonitorContainer::setDesktopNetworkHovered(bool hovered)
{
    if (this->desktop_network_hovered == hovered)
        return;

    this->desktop_network_hovered = hovered;
    if (hovered)
        this->map->setCursor(Qt::PointingHandCursor);
    else
        this->map->unsetCursor();
}

#if AOWIS_HAS_QRHI
void MapMonitorContainer::applyDesktopRhiSymbology()
{
    if (this->desktop_rhi_surface == nullptr)
        return;

    this->symbology_settings = this->symbology_settings.bounded();
    const NetworkSymbologyRanges ranges =
        this->hydraulic_data->symbologyRanges(this->symbology_settings);
    this->desktop_rhi_surface->setSymbology(resolveMapRhiSymbology(
        *this->hydraulic_data, this->symbology_settings, ranges));
}

void MapMonitorContainer::applyDesktopRhiHighlights()
{
    if (this->desktop_rhi_surface == nullptr || this->hydraulic_data == nullptr)
        return;

    this->desktop_rhi_surface->setSimulationErrorEntities(
        this->hydraulic_data->simulationErrorEntities(),
        this->hydraulic_data->simulationStaleDiagnosticEntityUuids());
}
#endif
#endif

void MapMonitorContainer::applySymbology()
{
    this->symbology_settings = this->symbology_settings.bounded();

#ifndef Q_OS_WASM
    const NetworkSymbologyRanges ranges =
        this->hydraulic_data->symbologyRanges(this->symbology_settings);
    this->desktop_network_overlay->setSymbology(this->symbology_settings, ranges);
#if AOWIS_HAS_QRHI
    if (this->desktop_rhi_surface != nullptr)
    {
        this->desktop_rhi_surface->setSymbology(resolveMapRhiSymbology(
            *this->hydraulic_data, this->symbology_settings, ranges));
    }
#endif
#else
    scheduleWasmNetworkSymbologySync();
#endif
}

void MapMonitorContainer::setNetworkBackgroundOpacity(int opacity)
{
    const int bounded_opacity = qBound(0, opacity, 100);
    if (this->network_background_opacity == bounded_opacity)
        return;

    this->network_background_opacity = bounded_opacity;
#ifndef Q_OS_WASM
    this->desktop_network_overlay->setBackgroundOpacity(bounded_opacity);
#if AOWIS_HAS_QRHI
    if (this->desktop_rhi_surface != nullptr)
        this->desktop_rhi_surface->setBackgroundOpacity(bounded_opacity);
#endif
#else
    syncWasmNetworkBackground();
#endif
}

#ifdef Q_OS_WASM
bool MapMonitorContainer::selectWasmNetworkEntityAt(const QPointF &position)
{
    if (this->hydraulic_data == nullptr || !this->wasm_network_snapshot_sent)
        return false;

    const double packed_hit = aowisBrowserNetworkHitTest(position.x(), position.y());
    if (!std::isfinite(packed_hit) || packed_hit <= 0.0)
    {
        this->wasm_selected_entity_type = InfrastructureEntity::Unknown;
        this->wasm_selected_entity_uuid = QUuid();
        aowisBrowserNetworkSetSelectedEntity(0, 0);
        return false;
    }

    const quint64 packed = static_cast<quint64>(packed_hit);
    const int entity_type_value = static_cast<int>(packed >> 32);
    const quint32 render_id = static_cast<quint32>(packed & 0xffffffffULL);
    const InfrastructureEntity entity_type = static_cast<InfrastructureEntity>(entity_type_value);
    const bool selected = selectNetworkEntity(render_id, entity_type);
    if (selected)
        aowisBrowserNetworkSetSelectedEntity(entity_type_value, render_id);
    else
        aowisBrowserNetworkSetSelectedEntity(0, 0);
    return selected;
}

void MapMonitorContainer::syncWasmSelectedEntity(InfrastructureEntity entity_type, const QUuid &uuid)
{
    this->wasm_selected_entity_type = entity_type;
    this->wasm_selected_entity_uuid = uuid;

    if (this->hydraulic_data == nullptr || uuid.isNull())
    {
        aowisBrowserNetworkSetSelectedEntity(0, 0);
        return;
    }

    const NetworkRenderSnapshot &snapshot = this->hydraulic_data->networkRenderSnapshot();
    if (InfrastructureEntityTraits::isHydraulicConnectionNode(entity_type))
    {
        for (const NetworkRenderNode &node : snapshot.nodes)
        {
            if (node.uuid == uuid && node.entity_type == entity_type)
            {
                aowisBrowserNetworkSetSelectedEntity(static_cast<int>(entity_type), node.render_id);
                return;
            }
        }
    }
    else if (InfrastructureEntityTraits::isHydraulicNetworkLink(entity_type))
    {
        for (const NetworkRenderLink &link : snapshot.links)
        {
            if (link.uuid == uuid && link.entity_type == entity_type)
            {
                aowisBrowserNetworkSetSelectedEntity(static_cast<int>(entity_type), link.render_id);
                return;
            }
        }
    }

    aowisBrowserNetworkSetSelectedEntity(0, 0);
}

void MapMonitorContainer::syncWasmSimulationErrorEntities()
{
    QJsonArray error_entities_json;
    if (this->hydraulic_data != nullptr)
    {
        const QHash<QUuid, InfrastructureEntity> error_entities = this->hydraulic_data->simulationErrorEntities();
        const NetworkRenderSnapshot &snapshot = this->hydraulic_data->networkRenderSnapshot();
        for (QHash<QUuid, InfrastructureEntity>::const_iterator error_iterator = error_entities.cbegin();
             error_iterator != error_entities.cend(); ++error_iterator)
        {
            const QUuid &uuid = error_iterator.key();
            const InfrastructureEntity entity_type = error_iterator.value();
            quint32 render_id = 0;
            if (InfrastructureEntityTraits::isHydraulicConnectionNode(entity_type))
            {
                for (const NetworkRenderNode &node : snapshot.nodes)
                {
                    if (node.uuid == uuid && node.entity_type == entity_type)
                    {
                        render_id = node.render_id;
                        break;
                    }
                }
            }
            else if (InfrastructureEntityTraits::isHydraulicNetworkLink(entity_type))
            {
                for (const NetworkRenderLink &link : snapshot.links)
                {
                    if (link.uuid == uuid && link.entity_type == entity_type)
                    {
                        render_id = link.render_id;
                        break;
                    }
                }
            }

            if (render_id == 0)
                continue;

            QJsonObject error_entity_json;
            error_entity_json.insert(QStringLiteral("renderId"), static_cast<int>(render_id));
            error_entity_json.insert(QStringLiteral("entityType"), static_cast<int>(entity_type));
            error_entity_json.insert(
                QStringLiteral("stale"),
                this->hydraulic_data->simulationDiagnosticEntityStale(uuid));
            error_entities_json.append(error_entity_json);
        }
    }

    const QByteArray json = QJsonDocument(error_entities_json).toJson(QJsonDocument::Compact);
    aowisBrowserNetworkSetErrorEntities(
        json.constData(), static_cast<int>(json.size()));
}

void MapMonitorContainer::scheduleWasmMapLayerSync()
{
    if (!this->wasm_map_layer_sync_timer->isActive())
        this->wasm_map_layer_sync_timer->start(0);
}

void MapMonitorContainer::scheduleWasmNetworkSymbologySync()
{
    this->wasm_network_symbology_sync_retry_count = 0;
    if (!this->wasm_network_symbology_sync_timer->isActive())
        this->wasm_network_symbology_sync_timer->start(0);
}

void MapMonitorContainer::syncWasmMapLayer()
{
    const bool visible = this->isVisible() && this->map->isVisible()
        && this->map->width() > 0 && this->map->height() > 0;

    if (!visible)
    {
        this->map->setBrowserMapLayerGeometry(QRect(), false);
        return;
    }

    const QRect map_geometry(this->map->mapToGlobal(QPoint(0, 0)), this->map->size());
    this->map->setBrowserMapLayerGeometry(map_geometry, true);
    this->syncWasmNetworkSnapshot();
}

void MapMonitorContainer::syncWasmNetworkBackground()
{
    const QColor background = this->map->palette().color(QPalette::Window);
    aowisBrowserNetworkSetBackground(
        background.red(),
        background.green(),
        background.blue(),
        this->network_background_opacity);
}

void MapMonitorContainer::syncWasmNetworkSnapshot()
{
    if (this->hydraulic_data == nullptr)
        return;

    const quint64 geometry_revision = this->hydraulic_data->geometryRevision();
    if (this->wasm_network_snapshot_sent &&
        this->wasm_network_geometry_revision_sent == geometry_revision)
        return;

    const NetworkRenderSnapshot &snapshot = this->hydraulic_data->networkRenderSnapshot();
    const QByteArray json = BrowserNetworkSnapshotSerializer::serialize(snapshot);
    const int transferred = aowisBrowserNetworkSetSnapshot(
        json.constData(), static_cast<int>(json.size()));
    if (transferred == 0)
        return;

    this->wasm_network_geometry_revision_sent = snapshot.geometry_revision;
    this->wasm_network_snapshot_sent = true;
    if (!this->wasm_selected_entity_uuid.isNull())
        syncWasmSelectedEntity(this->wasm_selected_entity_type, this->wasm_selected_entity_uuid);
    syncWasmSimulationErrorEntities();
    scheduleWasmNetworkSymbologySync();
}

void MapMonitorContainer::syncWasmNetworkSymbology()
{
    if (this->hydraulic_data == nullptr)
        return;

    const NetworkSymbologyRanges ranges =
        this->hydraulic_data->symbologyRanges(this->symbology_settings);
    const QByteArray json = BrowserNetworkSnapshotSerializer::serializeSymbology(
        *this->hydraulic_data, this->symbology_settings, ranges);
    const int transferred = aowisBrowserNetworkSetSymbology(
        json.constData(), static_cast<int>(json.size()));
    if (transferred != 0)
    {
        this->wasm_network_symbology_sync_retry_count = 0;
        return;
    }

    if (this->wasm_network_symbology_sync_retry_count < 20)
    {
        ++this->wasm_network_symbology_sync_retry_count;
        this->wasm_network_symbology_sync_timer->start(100);
        return;
    }

    qWarning() << "Could not transfer WASM network symbology. "
                  "The browser network JavaScript is missing setSymbology(); "
                  "rebuild and reload the WASM distribution.";
}
#endif

MapWidget *MapMonitorContainer::getMap()
{
    return this->map;
}
MapNavigationWidget *MapMonitorContainer::mapNavigationWidget()
{
    return this->map_menu->mapNavigationWidget();
}





MapMonitorMenuWidget::MapMonitorMenuWidget(MapWidget *map, QWidget *parent)
    : QWidget{parent},
    layout( new QVBoxLayout(this) ),
    map( map ),
    map_nav( new MapNavigationWidget(this->map, CanvasMode::Monitor, this->map, this) )
{
    setContentsMargins(0, 0, 0, 0);
    setMinimumWidth(Sizes::SidebarLeftWidth);
    setMaximumWidth(Sizes::SidebarLeftWidth);
    
    this->layout->addWidget(this->map_nav);
    
    addGroupVisualSettings();
    addGroupNodeVisuals();
    addGroupLinkVisuals();
    addGroupHeatmapVisuals();
    
    this->layout->addStretch();
}

MapNavigationWidget *MapMonitorMenuWidget::mapNavigationWidget()
{
    return this->map_nav;
}

void MapMonitorMenuWidget::addGroupVisualSettings()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Visual Settings", this);
    this->layout->addWidget(group);

    QVBoxLayout *vbox = new QVBoxLayout();
    group->setLayout(vbox);

    QLabel *label_node_section = new QLabel("<b>Node</b>");
    QLabel *label_node_size = new QLabel("Circle Size [%]");
    QSlider *slider_node_size = new SymbologySlider(50, 250, 100,
        QStringLiteral("Scales regular node circles only."), QStringLiteral(" %"), this);
    connect(slider_node_size, &QSlider::valueChanged, this, &MapMonitorMenuWidget::signalNodeSizeChanged);

    QLabel *label_link_section = new QLabel("<b>Link</b>");
    QLabel *label_link_thickness = new QLabel("Line Width [px]");
    QSlider *slider_link_thickness = new SymbologySlider(1, 12, 3,
        QStringLiteral("Sets the rendered link width and keeps link hit detection aligned with it."), QStringLiteral(" px"), this);
    connect(slider_link_thickness, &QSlider::valueChanged, this, &MapMonitorMenuWidget::signalLinkThicknessChanged);

    QLabel *label_flow_direction_size = new QLabel("Flow Arrows [px]");
    FlowDirectionSizeSlider *slider_flow_direction_size = new FlowDirectionSizeSlider(this);
    connect(slider_flow_direction_size, &QSlider::valueChanged, this,
        [this, slider_flow_direction_size](int)
    {
        emit signalFlowDirectionSizeChanged(slider_flow_direction_size->arrowSizePx());
    });

    QLabel *label_heatmap_section = new QLabel("<b>Heatmap</b>");
    QLabel *label_heatmap_radius = new QLabel("Radius");
    QComboBox *combo_heatmap_radius_unit = new QComboBox(this);
    combo_heatmap_radius_unit->addItem(QStringLiteral("m"),
        static_cast<int>(HeatmapRadiusUnit::Meters));
    combo_heatmap_radius_unit->addItem(QStringLiteral("px"),
        static_cast<int>(HeatmapRadiusUnit::Pixels));
    combo_heatmap_radius_unit->setToolTip(QStringLiteral(
        "Meters keep the heatmap radius geographic as the map zoom changes. "
        "Pixels keep a constant on-screen radius."));

    SymbologySlider *slider_heatmap_radius = new SymbologySlider(10, 1000, 400,
        QStringLiteral("Sets each heatmap point's geographic influence radius. It remains consistent in meters across zoom levels."),
        QStringLiteral(" m"), this);
    connect(slider_heatmap_radius, &QSlider::valueChanged, this,
        [this](int radius)
    {
        if (this->heatmap_radius_unit == HeatmapRadiusUnit::Pixels)
            this->heatmap_radius_px = radius;
        else
            this->heatmap_radius_m = radius;
        emit signalHeatmapRadiusChanged(this->heatmap_radius_unit, radius);
    });
    connect(combo_heatmap_radius_unit, &QComboBox::currentIndexChanged, this,
        [this, combo_heatmap_radius_unit, slider_heatmap_radius](int index)
    {
        const HeatmapRadiusUnit unit = static_cast<HeatmapRadiusUnit>(
            combo_heatmap_radius_unit->itemData(index).toInt());
        if (unit == this->heatmap_radius_unit)
            return;

        this->heatmap_radius_unit = unit;
        if (unit == HeatmapRadiusUnit::Pixels)
        {
            slider_heatmap_radius->setConfiguration(5, 250, 50, this->heatmap_radius_px,
                QStringLiteral("Sets each heatmap point's on-screen influence radius. It remains constant in pixels across zoom levels."),
                QStringLiteral(" px"));
        }
        else
        {
            slider_heatmap_radius->setConfiguration(10, 1000, 400, this->heatmap_radius_m,
                QStringLiteral("Sets each heatmap point's geographic influence radius. It remains consistent in meters across zoom levels."),
                QStringLiteral(" m"));
        }
        emit signalHeatmapRadiusUnitChanged(unit);
    });

    QLabel *label_heatmap_solid_center = new QLabel("Solid Center [%]");
    QSlider *slider_heatmap_solid_center = new SymbologySlider(0, 100, 70,
        QStringLiteral("Sets how much of the heatmap radius keeps full local opacity before fading to transparency."), QStringLiteral(" %"), this);
    connect(slider_heatmap_solid_center, &QSlider::valueChanged, this, &MapMonitorMenuWidget::signalHeatmapSolidCenterChanged);

    QLabel *label_heatmap_opacity = new QLabel("Opacity [%]");
    QSlider *slider_heatmap_opacity = new SymbologySlider(0, 100, 75,
        QStringLiteral("Controls the opacity of the complete heatmap layer."), QStringLiteral(" %"), this);
    connect(slider_heatmap_opacity, &QSlider::valueChanged, this, &MapMonitorMenuWidget::signalHeatmapOpacityChanged);

    vbox->addWidget(label_node_section);
    vbox->addWidget(label_node_size);
    vbox->addWidget(slider_node_size);
    vbox->addSpacing(4);
    vbox->addWidget(label_link_section);
    vbox->addWidget(label_link_thickness);
    vbox->addWidget(slider_link_thickness);
    vbox->addWidget(label_flow_direction_size);
    vbox->addWidget(slider_flow_direction_size);
    vbox->addSpacing(4);
    vbox->addWidget(label_heatmap_section);
    QHBoxLayout *heatmap_radius_header = new QHBoxLayout();
    heatmap_radius_header->addWidget(label_heatmap_radius);
    heatmap_radius_header->addStretch();
    heatmap_radius_header->addWidget(combo_heatmap_radius_unit);
    vbox->addLayout(heatmap_radius_header);
    vbox->addWidget(slider_heatmap_radius);
    vbox->addWidget(label_heatmap_solid_center);
    vbox->addWidget(slider_heatmap_solid_center);
    vbox->addWidget(label_heatmap_opacity);
    vbox->addWidget(slider_heatmap_opacity);
}

void MapMonitorMenuWidget::addGroupNodeVisuals()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Node Symbology", this);
    this->layout->addWidget(group);
    QVBoxLayout *vbox = new QVBoxLayout();
    group->setLayout(vbox);
    
    QRadioButton *radio_node_none = new QRadioButton("None");
    radio_node_none->setChecked(true);
    QRadioButton *radio_node_elevation = new QRadioButton("Elevation");
    QRadioButton *radio_node_basedemand = new QRadioButton("Base Demand");
    QRadioButton *radio_node_totaldemand = new QRadioButton("Total Demand");
    QRadioButton *radio_node_demanddeficit = new QRadioButton("Demand Deficit");
    QRadioButton *radio_node_emitterflow = new QRadioButton("Emitter Flow");
    QRadioButton *radio_node_leakage = new QRadioButton("Leakage");
    QRadioButton *radio_node_head = new QRadioButton("Head");
    QRadioButton *radio_node_pressure = new QRadioButton("Pressure");
    QRadioButton *radio_node_water_age = new QRadioButton("Water Age [h]");
    
    QLabel *label_multispecies = new QLabel(
        "EPANET Network 3 has<br>"
        "Multi-Species Water<br>"
        "Quality Analysis (MSX).<br>"
        "Modeled variables are:"
    );
    
    QRadioButton *radio_node_chlorine = new QRadioButton("Cl₂ [mg/L]");
    QRadioButton *radio_node_river = new QRadioButton("River Water [%]");
    QRadioButton *radio_node_lake = new QRadioButton("Lake Water [%]");
    
    QLabel *label_chlorine = new QLabel(
        "Chlorine decay constant<br>"
        "depends on the mix of<br>"
        "Lake and River water."
    );
    
    const std::function<void(QRadioButton *, VisualNode)> connect_node_visual =
        [this](QRadioButton *button, VisualNode visual)
    {
        connect(button, &QRadioButton::clicked, this, [this, visual]
        {
            emit signalNodeVisualClicked(visual);
        });
    };
    connect_node_visual(radio_node_none, VisualNode::None);
    connect_node_visual(radio_node_elevation, VisualNode::Elevation);
    connect_node_visual(radio_node_basedemand, VisualNode::BaseDemand);
    connect_node_visual(radio_node_totaldemand, VisualNode::TotalDemand);
    connect_node_visual(radio_node_demanddeficit, VisualNode::DemandDeficit);
    connect_node_visual(radio_node_emitterflow, VisualNode::EmitterFlow);
    connect_node_visual(radio_node_leakage, VisualNode::Leakage);
    connect_node_visual(radio_node_head, VisualNode::Head);
    connect_node_visual(radio_node_pressure, VisualNode::Pressure);
    connect_node_visual(radio_node_water_age, VisualNode::WaterAge);
    connect_node_visual(radio_node_chlorine, VisualNode::Chlorine);
    connect_node_visual(radio_node_river, VisualNode::RiverWater);
    connect_node_visual(radio_node_lake, VisualNode::LakeWater);
    
    vbox->addWidget(radio_node_none);
    vbox->addWidget(radio_node_elevation);
    vbox->addWidget(radio_node_basedemand);
    vbox->addWidget(radio_node_totaldemand);
    vbox->addWidget(radio_node_demanddeficit);
    vbox->addWidget(radio_node_emitterflow);
    vbox->addWidget(radio_node_leakage);
    vbox->addWidget(radio_node_head);
    vbox->addWidget(radio_node_pressure);
    vbox->addWidget(radio_node_water_age);

    vbox->addWidget(label_multispecies);
    
    vbox->addWidget(radio_node_chlorine);
    vbox->addWidget(radio_node_river);
    vbox->addWidget(radio_node_lake);
    
    vbox->addWidget(label_chlorine);
}
void MapMonitorMenuWidget::addGroupLinkVisuals()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Link Symbology", this);
    this->layout->addWidget(group);
    QVBoxLayout *vbox = new QVBoxLayout();
    group->setLayout(vbox);
    
    QRadioButton *radio_link_none = new QRadioButton("None");
    radio_link_none->setChecked(true);
    QRadioButton *radio_link_diameter = new QRadioButton("Diameter");
    QRadioButton *radio_link_length = new QRadioButton("Length");
    QRadioButton *radio_link_roughness = new QRadioButton("Roughness");
    QRadioButton *radio_link_flowrate = new QRadioButton("Flow Rate");
    QRadioButton *radio_link_velocity = new QRadioButton("Velocity");
    QRadioButton *radio_link_headloss = new QRadioButton("Head Loss");
    QRadioButton *radio_link_leakage = new QRadioButton("Leakage");
    QRadioButton *radio_link_water_age = new QRadioButton("Water Age [h]");
    QRadioButton *radio_link_chlorine = new QRadioButton("Cl₂ [mg/L]");
    QRadioButton *radio_link_river = new QRadioButton("River Water [%]");
    QRadioButton *radio_link_lake = new QRadioButton("Lake Water [%]");
    
    const std::function<void(QRadioButton *, VisualLink)> connect_link_visual =
        [this](QRadioButton *button, VisualLink visual)
    {
        connect(button, &QRadioButton::clicked, this, [this, visual]
        {
            emit signalLinkVisualClicked(visual);
        });
    };
    connect_link_visual(radio_link_none, VisualLink::None);
    connect_link_visual(radio_link_diameter, VisualLink::Diameter);
    connect_link_visual(radio_link_length, VisualLink::Length);
    connect_link_visual(radio_link_roughness, VisualLink::Roughness);
    connect_link_visual(radio_link_flowrate, VisualLink::FlowRate);
    connect_link_visual(radio_link_velocity, VisualLink::Velocity);
    connect_link_visual(radio_link_headloss, VisualLink::HeadLoss);
    connect_link_visual(radio_link_leakage, VisualLink::Leakage);
    connect_link_visual(radio_link_water_age, VisualLink::WaterAge);
    connect_link_visual(radio_link_chlorine, VisualLink::Chlorine);
    connect_link_visual(radio_link_river, VisualLink::RiverWater);
    connect_link_visual(radio_link_lake, VisualLink::LakeWater);
    
    vbox->addWidget(radio_link_none);
    vbox->addWidget(radio_link_diameter);
    vbox->addWidget(radio_link_length);
    vbox->addWidget(radio_link_roughness);
    vbox->addWidget(radio_link_flowrate);
    vbox->addWidget(radio_link_velocity);
    vbox->addWidget(radio_link_headloss);
    vbox->addWidget(radio_link_leakage);
    vbox->addWidget(radio_link_water_age);
    vbox->addWidget(radio_link_chlorine);
    vbox->addWidget(radio_link_river);
    vbox->addWidget(radio_link_lake);
}

void MapMonitorMenuWidget::addGroupHeatmapVisuals()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Heatmap", this);
    this->layout->addWidget(group);
    
    QVBoxLayout *vbox = new QVBoxLayout();
    group->setLayout(vbox);
    
    QRadioButton *radio_none = new QRadioButton("None");
    radio_none->setChecked(true);
    
    QRadioButton *radio_elevation = new QRadioButton("Elevation");
    QRadioButton *radio_base_demand = new QRadioButton("Base Demand");
    QRadioButton *radio_total_demand = new QRadioButton("Total Demand");
    QRadioButton *radio_demand_deficit = new QRadioButton("Demand Deficit");
    QRadioButton *radio_emitter_flow = new QRadioButton("Emitter Flow");
    QRadioButton *radio_leakage = new QRadioButton("Leakage");
    QRadioButton *radio_head = new QRadioButton("Head");
    QRadioButton *radio_pressure = new QRadioButton("Pressure");
    QRadioButton *radio_water_age = new QRadioButton("Water Age [h]");
    QRadioButton *radio_chlorine = new QRadioButton("Cl₂ [mg/L]");
    QRadioButton *radio_river = new QRadioButton("River Water [%]");
    QRadioButton *radio_lake = new QRadioButton("Lake Water [%]");
    
    const std::function<void(QRadioButton *, VisualHeatmap)> connect_heatmap_visual =
        [this](QRadioButton *button, VisualHeatmap visual)
    {
        connect(button, &QRadioButton::clicked, this, [this, visual]
        {
            emit signalHeatmapVisualClicked(visual);
        });
    };
    
    connect_heatmap_visual(radio_none, VisualHeatmap::None);
    connect_heatmap_visual(radio_elevation, VisualHeatmap::Elevation);
    connect_heatmap_visual(radio_base_demand, VisualHeatmap::BaseDemand);
    connect_heatmap_visual(radio_total_demand, VisualHeatmap::TotalDemand);
    connect_heatmap_visual(radio_demand_deficit, VisualHeatmap::DemandDeficit);
    connect_heatmap_visual(radio_emitter_flow, VisualHeatmap::EmitterFlow);
    connect_heatmap_visual(radio_leakage, VisualHeatmap::Leakage);
    connect_heatmap_visual(radio_head, VisualHeatmap::Head);
    connect_heatmap_visual(radio_pressure, VisualHeatmap::Pressure);
    connect_heatmap_visual(radio_water_age, VisualHeatmap::WaterAge);
    connect_heatmap_visual(radio_chlorine, VisualHeatmap::Chlorine);
    connect_heatmap_visual(radio_river, VisualHeatmap::RiverWater);
    connect_heatmap_visual(radio_lake, VisualHeatmap::LakeWater);

    vbox->addWidget(radio_none);
    vbox->addWidget(radio_elevation);
    vbox->addWidget(radio_base_demand);
    vbox->addWidget(radio_total_demand);
    vbox->addWidget(radio_demand_deficit);
    vbox->addWidget(radio_emitter_flow);
    vbox->addWidget(radio_leakage);
    vbox->addWidget(radio_head);
    vbox->addWidget(radio_pressure);
    vbox->addWidget(radio_water_age);
    vbox->addWidget(radio_chlorine);
    vbox->addWidget(radio_river);
    vbox->addWidget(radio_lake);
}
