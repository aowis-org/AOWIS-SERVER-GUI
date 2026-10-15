#ifndef MAP_BGFX_HUD_H
#define MAP_BGFX_HUD_H

#include "map/bgfx/map_bgfx_2d_projection.h"
#include "map/bgfx/map_bgfx_hud_canvas.h"
#include "map/bgfx/map_bgfx_hud_text_cache.h"

#include <QColor>
#include <QFont>
#include <QImage>
#include <QPalette>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <functional>

// Native bgfx HUD of the 2D Map Monitor (Phase 1 slice 6a).
//
// Replaces the QWidget HUD that cannot sit on top of bgfx's native window:
// crosshair, GPS position, scale bar, download activity (map tiles, terrain)
// with cancel buttons, the 2D/3D view switch and the fullscreen button. The
// map legend follows in slice 6b; globe-only controls (compass, tilt, ...)
// with the bgfx globe.
//
// Same content, positions and sizes as the QWidget HUD
// (MapMonitorContainer::positionDesktopHudWidgets(), map_monitor_hud_controls),
// drawn as one batch per frame into MapBgfxHudCanvas with text runs from
// MapBgfxHudTextCache. Layout is rebuilt every frame from the current state
// (a handful of rectangles); the hit boxes of the last build answer mouse
// input, so what is clickable is exactly what was drawn.
//
// Pure Qt Gui + math: no bgfx, no MapModel, testable without a GPU.
class MapBgfxHud
{
public:
    enum class Action
    {
        None,
        CancelMapTiles,
        CancelTerrain,
        ViewMode2d,
        ViewMode3d,
        ToggleFullscreen
    };

    // Colours of the QWidget HUD (configureHudFrame(): window colour at
    // alpha 220, the palette's text/button roles).
    struct Palette
    {
        QRgb panel = qRgba(239, 239, 239, 220);
        QRgb border = qRgb(160, 160, 160);
        QRgb text = qRgb(0, 0, 0);
        QRgb button = qRgb(239, 239, 239);
        QRgb button_text = qRgb(0, 0, 0);
        QRgb disabled_text = qRgb(150, 150, 150);
        QRgb highlight = qRgb(48, 140, 198);
        QRgb highlighted_text = qRgb(255, 255, 255);

        static Palette fromQPalette(const QPalette &palette);
    };

    // Scale bar (2D): for a horizontal sample of bar_px pixels from
    // (lon0, lat0) to (lon1, lat1), the nice bar length in pixels and its
    // label in the user's distance unit. valid=false hides the bar.
    struct ScaleBar
    {
        bool valid = false;
        double width_px = 0.0;
        QString label;
    };
    using ScaleProvider = std::function<ScaleBar(double lon0, double lat0, double lon1,
                                                 double lat1, double bar_px)>;

    struct FrameInputs
    {
        QSizeF viewport;
        bool two_d = true;
        // 2D camera (continuous zoom), used for the GPS dot and the scale.
        MapBgfx2d::Camera camera;
        // Monotonic seconds, for the busy indicators.
        double time_s = 0.0;
    };

    // --- State ----------------------------------------------------------------
    void setFont(const QFont &font);
    void setScaleProvider(ScaleProvider provider) { this->scale_provider = std::move(provider); }
    void setImages(const QImage &crosshair, const QImage &fullscreen, const QImage &fullscreen_exit);
    void setGps(bool valid, double longitude_deg, double latitude_deg);
    void setDownloadActivity(int tiles_active, int tiles_queued,
                             int terrain_active, int terrain_queued);
    void setViewMode(bool two_d, bool globe_available);
    void setFullscreen(bool available, bool active);

    // --- Frame ----------------------------------------------------------------
    // Builds the HUD into canvas (cleared first). Rebuilds once if the text
    // atlas had to be cleared during the build (earlier sprites are stale).
    void build(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
               const FrameInputs &inputs, const Palette &palette);

    // --- Input (logical pixels) -----------------------------------------------
    // A press on a HUD panel is consumed (never reaches map navigation or
    // picking); on an enabled button it arms that button.
    bool mousePress(double x, double y);
    // Consumed iff the matching press was. Returns the button's action when
    // the release is on the armed button.
    Action mouseRelease(double x, double y, bool *consumed);
    // Updates hover. Consumed while a HUD press is held. over_button: the
    // pointer is over an enabled button (pointing-hand cursor).
    bool mouseMove(double x, double y, bool *over_button);
    // The pointer left the map: drop the hover highlight (a held press
    // stays armed; the window keeps the implicit grab).
    void clearHover() { this->hovered = Element::None; }
    // Any blocking HUD panel at (x, y).
    bool blocksInput(double x, double y) const;

    // Layout constants (logical px) of the QWidget HUD.
    static constexpr double MarginPx = 12.0;
    static constexpr double ScaleWidthPx = 116.0;
    static constexpr double ScaleHeightPx = 42.0;
    static constexpr double ScalePaddingPx = 6.0;
    static constexpr double ScaleTickHeightPx = 6.0;
    static constexpr double CrosshairExtentPx = 60.0;
    static constexpr double GpsRadiusPx = 5.0;

private:
    enum class Element
    {
        None,
        CancelMapTiles,
        CancelTerrain,
        ViewMode2d,
        ViewMode3d,
        Fullscreen
    };
    struct HitBox
    {
        MapBgfxHudRect rect;
        Element element = Element::None;
        bool enabled = false;
    };

    void buildOnce(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                   const FrameInputs &inputs, const Palette &palette);
    void drawPanel(MapBgfxHudCanvas *canvas, const MapBgfxHudRect &rect, const Palette &palette);
    void drawButton(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                    const MapBgfxHudRect &rect, const QString &label, Element element,
                    bool enabled, bool checked, const Palette &palette);
    double buttonWidth(MapBgfxHudTextCache *cache, const QString &label) const;
    double buttonHeight(MapBgfxHudTextCache *cache) const;
    // Returns the panel width actually used (layout pass when canvas null).
    double downloadPanel(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache, double x, double y,
                         const QString &title, int active, int queued, Element cancel,
                         bool cancel_pending, double time_s, const Palette &palette);
    void scaleBar(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                  const FrameInputs &inputs, const Palette &palette);
    Element elementAt(double x, double y) const;
    static Action actionFor(Element element);

    QFont font;
    QFont bold_font;
    QFont scale_font;
    ScaleProvider scale_provider;
    QImage crosshair_image;
    QImage fullscreen_image;
    QImage fullscreen_exit_image;
    bool gps_valid = false;
    double gps_longitude_deg = 0.0;
    double gps_latitude_deg = 0.0;
    int tiles_active = 0;
    int tiles_queued = 0;
    int terrain_active = 0;
    int terrain_queued = 0;
    // A clicked Cancel stays disabled until the activity changes (QWidget HUD).
    bool tiles_cancel_pending = false;
    bool terrain_cancel_pending = false;
    bool two_d = true;
    bool globe_available = false;
    bool fullscreen_available = false;
    bool fullscreen_active = false;

    QVector<HitBox> hit_boxes;
    QVector<MapBgfxHudRect> blocking_panels;
    Element hovered = Element::None;
    Element armed = Element::None;
    bool press_consumed = false;
};

#endif // MAP_BGFX_HUD_H
