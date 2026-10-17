#ifndef TAB_MAP_EDITOR_CONTAINER_H
#define TAB_MAP_EDITOR_CONTAINER_H

#include <QObject>
#include <QPointF>
#include <QWidget>

#include <QEvent>
#include <QTimer>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStackedLayout>
#include <QScrollArea>
#include <QToolBox>
#include <QToolButton>
#include <QRadioButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QKeySequence>

#include "map/core/map_model.h"
#include "map/data/map_tile_repository.h"
#include "map/core/map_widget.h"
#include "map/core/map_navigation_widget.h"
#include "map/editor/map_canvas_widget.h"
#include "map/editor/map_editor_controller.h"
#include "entity_inspector/entity_inspector_dock.h"
#include "network/hydraulic_data.h"

#ifdef Q_OS_WASM
#include "gps/gps_provider_dummy.h"
#else
#include "gps/gps_provider.h"
#endif

#include "common/_sizes.h"
#include "common/_enums_structs.h"

#include <QDebug>

// The map editor draws with bgfx only (roadmap Phase 2; the QPainter editor
// and the QRhi editor path are gone): imagery, the network, every editor
// overlay, map labels and the HUD. Without bgfx in the build, or when it
// cannot start, a status label takes the map's place.
class MapBgfxSurface;
class QKeyEvent;
class QShortcut;

class MapEditorMenuWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MapEditorMenuWidget(MapWidget *map, MapCanvasWidget *map_canvas,
                                 MapEditorController *editor_controller, CanvasMode mode,
                                 QWidget *parent = nullptr);
    
    MapNavigationWidget *mapNavigationWidget();
    bool isEditNetworkSectionActive() const;
    // Map fullscreen: the left menu is not visible, so its buttons' window
    // shortcuts (tools, map providers) cannot fire. The map forwards key
    // presses here instead; true when a button's shortcut matched (the
    // button was clicked).
    bool triggerButtonShortcut(const QKeyEvent *event);
    
private:
    CanvasMode mode;
    QVBoxLayout *layout;
    
    MapWidget *map;
    MapNavigationWidget *map_nav;
    MapCanvasWidget *map_canvas;
    MapEditorController *editor_controller;
    
    QSpinBox *spin_zoom_from;
    QSpinBox *spin_zoom_to;
    
    QToolBox *toolbox;
    int toolbox_edit_index = -1;
    int toolbox_cache_index = -1;
    void createToolboxCache(QToolBox *tbx);
    void createToolboxEdit(QToolBox *tbx);
    void setToolboxMode(int index);
    void updateToolboxHeight(int index);
    
    QButtonGroup *button_group_tools = nullptr;
    QRadioButton *button_radio_select = nullptr;
    QToolButton *button_tiles_delete = nullptr;
    QCheckBox *checkbox_map_editor_guide = nullptr;

public slots:
    void setMapEditorGuideChecked(bool checked);
    
signals:
    void signalSlideOpacityChanged(int opacity);
    void signalMapEditorGuideVisibilityChanged(bool visible);
    void signalEditNetworkSectionActive(bool active);
};



class MapEditorContainer : public QWidget
{
    Q_OBJECT
public:
    explicit MapEditorContainer(MapModel *map_model, MapTileRepository *tile_repository, HydraulicData *hydraulic_data, GpsProvider *gps, EntityInspectorDock *map_inspector, QWidget *parent = nullptr);
    ~MapEditorContainer() override;
    
    MapWidget *getMap();
    
    MapNavigationWidget *mapNavigationWidget();
    bool isEditNetworkSectionActive() const;

public slots:
    void setMapEditorGuideChecked(bool checked);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    HydraulicData *hydraulic_data = nullptr;
    GpsProvider *gps;
    
    EntityInspectorDock *map_inspector;
    
    MapModel *map_model;
    MapTileRepository *tile_repository;
    QWidget *map_stack;
    QStackedLayout *map_stack_layout;
    MapWidget *map;
    MapCanvasWidget *map_canvas;
    // Shown instead of the map until bgfx presents its first frame, and
    // when it cannot (no bgfx in the build, startup failure).
    QLabel *renderer_status;
    void showRendererStatus(const QString &text);
    MapBgfxSurface *bgfx_surface = nullptr;
    bool bgfx_editor_active = false;
    // The editor's visual state for the bgfx overlay, refreshed when the
    // canvas reports a change (MapCanvasWidget::signalRenderUpdateRequested)
    // instead of being rebuilt every frame.
    MapEditorVisualState bgfx_visual_state;
    bool bgfx_visual_state_dirty = true;
    // Only the camera moved: refresh the pointer's map coordinate.
    bool bgfx_visual_camera_dirty = false;
    // Pointer shape of the HUD element under the pointer (buttons); wins
    // over the editor tool's cursor.
    Qt::CursorShape bgfx_hud_cursor = Qt::ArrowCursor;
    // Map fullscreen (desktop): the HUD button and the map fullscreen
    // shortcut, the monitor's reparenting.
    QShortcut *bgfx_fullscreen_shortcut = nullptr;
    bool bgfx_fullscreen_active = false;
    void setupBgfxEditor();
    // HUD clicks (queued from the surface): 2D/3D switch, fullscreen.
    void handleBgfxHudAction(int action);
    void setBgfxFullscreen(bool fullscreen);
    void applyEditorCursor();
    // HUD first for pointer, wheel and Escape on the canvas (desktop: the
    // events forwarded from the native window; WASM: the canvas' own).
    bool routeBgfxHudEvent(QEvent *event);

    // Network snapshot, editor symbology and move preview (hidden entities,
    // full-network translation) of the bgfx map.
    void connectRenderSurfaceSync();
    void syncRenderSurfaceEditorState();
    void syncRenderSurfaceNetworkSnapshot();
    void applyRenderSurfaceEditorSymbology();
    quint64 render_move_session_id = 0;
    QPointF render_move_start_mouse_position;
    bool render_full_network_move = false;
    QPointF render_move_translation;
    MapEditorController *editor_controller;
    MapEditorMenuWidget *map_menu;
    
    QHBoxLayout *layout;
    
signals:
    void signalMapEditorGuideVisibilityChanged(bool visible);
    void signalEditNetworkSectionActive(bool active);
};

#endif // TAB_MAP_EDITOR_CONTAINER_H
