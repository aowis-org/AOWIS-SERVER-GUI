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

#include <array>
#include <functional>

// Native bgfx HUD of the 2D Map Monitor (Phase 1 slices 6a and 6b).
//
// Replaces the QWidget HUD that cannot sit on top of bgfx's native window:
// crosshair, GPS position, scale bar, download activity (map tiles, terrain)
// with cancel buttons, the 2D/3D view switch, the fullscreen button (6a) and
// the map legend (6b: node / link / heatmap metric combos, colour ramps with
// value labels, hover value swatch, palette menu with flip and reset,
// tooltips). Globe-only controls (compass, tilt, ...) follow with the bgfx
// globe.
//
// Same content, positions and sizes as the QWidget HUD
// (MapMonitorContainer::positionDesktopHudWidgets(), map_monitor_hud_controls,
// EntityMapLegendHud), drawn as one batch per frame into MapBgfxHudCanvas
// with text runs from MapBgfxHudTextCache. Layout is rebuilt every frame
// from the current state (a handful of rectangles); the hit boxes of the
// last build answer mouse input, so what is clickable is exactly what was
// drawn. Popups (combo lists, palette menu) and tooltips are HUD state too:
// they are drawn last, on top, and take input first.
//
// Pure Qt Gui + math: no bgfx, no MapModel, no HydraulicData, testable
// without a GPU. The legend's content (items, ranges, units, palette
// colours) is computed by the application (map_bgfx_legend_content.h) and
// pushed in with setLegend().
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
        ToggleFullscreen,
        // Legend: Command::group selects node (0) / link (1) / heatmap (2).
        // LegendVisual: Command::value is the chosen metric (VisualNode /
        // VisualLink / VisualHeatmap as int).
        LegendVisual,
        // LegendPalette: Command::value is the NetworkSymbologyPalette as
        // int, Command::flipped its direction.
        LegendPalette
    };

    // An action plus its legend arguments (unused fields stay default).
    struct Command
    {
        Action action = Action::None;
        int group = -1;
        int value = 0;
        bool flipped = false;
    };

    // Pointer shape the map should show for the HUD element under it.
    enum class Cursor
    {
        Arrow,
        PointingHand,
        Cross
    };

    // Colours of the QWidget HUD (configureHudFrame(): window colour at
    // alpha 220, the palette's text/button roles; QMenu/QComboBox popups and
    // QToolTip use their own roles).
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
        // Opaque popup backgrounds (QMenu: Window, combo list: Base).
        QRgb window = qRgb(239, 239, 239);
        QRgb base = qRgb(255, 255, 255);
        // Ramp without finite values (QPalette::AlternateBase).
        QRgb alternate_base = qRgb(247, 247, 247);
        QRgb tooltip = qRgb(255, 255, 220);
        QRgb tooltip_text = qRgb(0, 0, 0);

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

    // --- Legend content (slice 6b) ----------------------------------------------
    // One colour ramp as EntityMapLegendHud's MapSymbologyRampWidget shows
    // it: displayed range and unit (already converted to the user's unit),
    // the active palette's stops with the flip applied, and the palette
    // selection for the menu's checked states and its Reset.
    struct LegendRamp
    {
        // A metric is chosen: the ramp row is shown under the combo.
        bool visible = false;
        double minimum = 0.0;
        double maximum = 0.0;
        QString unit;
        // Evenly spaced stops (0xAARRGGBB), flip already applied.
        QVector<QRgb> colors;
        int palette = 0;
        bool flipped = false;
        int default_palette = 0;
        bool default_flipped = false;
    };
    struct LegendGroup
    {
        // Combo entries (text, metric value), in display order.
        QVector<QString> items;
        QVector<int> values;
        int current = 0;
        LegendRamp ramp;
    };
    struct LegendPaletteChoice
    {
        int value = 0;
        QString name;
        // Unflipped stops (0xAARRGGBB) for the menu preview.
        QVector<QRgb> colors;
        // Diverging palettes get their own menu section.
        bool diverging = false;
    };
    struct Legend
    {
        bool visible = false;
        // Node, link, heatmap.
        std::array<LegendGroup, 3> groups;
        QVector<LegendPaletteChoice> palettes;
    };
    static constexpr int LegendGroupCount = 3;

    struct FrameInputs
    {
        QSizeF viewport;
        bool two_d = true;
        // 2D camera (continuous zoom), used for the GPS dot and the scale.
        MapBgfx2d::Camera camera;
        // Monotonic seconds, for the busy indicators and the tooltip delay.
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
    // The 2D/3D switch (default shown). The map editor has none.
    void setViewSwitchVisible(bool visible) { this->view_switch_visible = visible; }
    void setFullscreen(bool available, bool active);
    // Replaces the legend content. Open popups stay open when their group
    // still exists; hiding the legend closes them.
    void setLegend(const Legend &legend);
    const Legend &legend() const { return this->legend_content; }

    // --- Frame ----------------------------------------------------------------
    // Builds the HUD into canvas (cleared first). Rebuilds once if the text
    // atlas had to be cleared during the build (earlier sprites are stale).
    void build(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
               const FrameInputs &inputs, const Palette &palette);

    // --- Input (logical pixels) -----------------------------------------------
    // A press on a HUD panel is consumed (never reaches map navigation or
    // picking); on an enabled button it arms that button. A press on a
    // legend combo opens its list, on a colour ramp the palette menu (both
    // on press, like QComboBox / the QWidget ramp). While a popup is open,
    // a press outside it closes it and is consumed (Qt popup behaviour).
    bool mousePress(double x, double y);
    // Consumed iff the matching press was. Returns the button's action when
    // the release is on the armed button; command (optional) receives the
    // action with its legend arguments.
    Action mouseRelease(double x, double y, bool *consumed, Command *command = nullptr);
    // Updates hover (and drags the combo list's scroll bar). Consumed while
    // a HUD press is held. over_button: the pointer is over an enabled
    // button (pointing-hand cursor); cursor() has the exact shape.
    bool mouseMove(double x, double y, bool *over_button);
    // Wheel over the HUD (angle delta in 1/8 degree, positive = away from
    // the user). Scrolls an open combo list; consumed over any HUD panel
    // so the map does not zoom underneath.
    bool mouseWheel(double x, double y, double angle_delta_y);
    // The pointer left the map: drop the hover highlight (a held press
    // stays armed; the window keeps the implicit grab).
    void clearHover();
    // Escape: closes an open popup. True when one was open.
    bool closePopup();
    bool popupOpen() const { return this->open_combo >= 0 || this->open_palette_menu >= 0; }
    // Any blocking HUD panel at (x, y).
    bool blocksInput(double x, double y) const;
    Cursor cursor() const;

    // Same number formatting as the QWidget legend ramp (three significant
    // digits, scientific notation outside 1e-3 .. 1e3, locale aware).
    static QString formatLegendValue(double value);

    // Layout constants (logical px) of the QWidget HUD.
    static constexpr double MarginPx = 12.0;
    static constexpr double ScaleWidthPx = 116.0;
    static constexpr double ScaleHeightPx = 42.0;
    static constexpr double ScalePaddingPx = 6.0;
    static constexpr double ScaleTickHeightPx = 6.0;
    static constexpr double CrosshairExtentPx = 60.0;
    static constexpr double GpsRadiusPx = 5.0;
    // EntityMapLegendHud: fixed width Sizes::SidebarRightWidth, contents
    // margins 7, spacing 4; MapSymbologyRampWidget 54 px high with a 16 px
    // ramp inset 7/4; hover swatch 96 x 52.
    static constexpr double LegendWidthPx = 380.0;
    static constexpr double LegendPaddingPx = 7.0;
    static constexpr double LegendSpacingPx = 4.0;
    static constexpr double LegendRampRowHeightPx = 54.0;
    static constexpr double LegendRampInsetXPx = 7.0;
    static constexpr double LegendRampInsetYPx = 4.0;
    static constexpr double LegendRampHeightPx = 16.0;
    static constexpr double LegendSwatchWidthPx = 96.0;
    static constexpr double LegendSwatchHeightPx = 52.0;
    // QToolTip wake-up delay.
    static constexpr double TooltipDelayS = 0.7;

private:
    enum class Element
    {
        None,
        CancelMapTiles,
        CancelTerrain,
        ViewMode2d,
        ViewMode3d,
        Fullscreen,
        // Legend (group = node/link/heatmap, index = item / palette choice).
        LegendCombo,
        LegendRampRow,
        LegendRamp,
        LegendComboItem,
        LegendComboScroll,
        LegendPalette,
        LegendPaletteFlip,
        LegendPaletteReset,
        // Background of an open popup: blocks what lies underneath
        // (Target::index: PopupComboList or PopupPaletteMenu).
        Popup
    };
    static constexpr int PopupComboList = 0;
    static constexpr int PopupPaletteMenu = 1;
    struct Target
    {
        Element element = Element::None;
        int group = -1;
        int index = -1;

        bool operator==(const Target &other) const
        {
            return this->element == other.element && this->group == other.group
                && this->index == other.index;
        }
        bool operator!=(const Target &other) const { return !(*this == other); }
    };
    struct HitBox
    {
        MapBgfxHudRect rect;
        Target target;
        bool enabled = false;
        Cursor cursor = Cursor::Arrow;
        QString tooltip;
    };

    void buildOnce(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                   const FrameInputs &inputs, const Palette &palette);
    void drawPanel(MapBgfxHudCanvas *canvas, const MapBgfxHudRect &rect, const Palette &palette);
    void drawButton(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                    const MapBgfxHudRect &rect, const QString &label, const Target &target,
                    bool enabled, bool checked, const Palette &palette,
                    const QString &tooltip = QString());
    double buttonWidth(MapBgfxHudTextCache *cache, const QString &label) const;
    double buttonHeight(MapBgfxHudTextCache *cache) const;
    // Returns the panel width actually used (layout pass when canvas null).
    double downloadPanel(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache, double x, double y,
                         const QString &title, int active, int queued, Element cancel,
                         bool cancel_pending, double time_s, const Palette &palette);
    void scaleBar(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                  const FrameInputs &inputs, const Palette &palette);

    // --- Legend (slice 6b) -------------------------------------------------------
    double comboHeight(MapBgfxHudTextCache *cache) const;
    double comboItemHeight(MapBgfxHudTextCache *cache) const;
    void legendPanel(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                     const FrameInputs &inputs, const Palette &palette);
    void drawCombo(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                   const MapBgfxHudRect &rect, int group, const Palette &palette);
    void drawRampRow(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                     const MapBgfxHudRect &row, int group, const Palette &palette);
    void drawRampLabels(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                        const MapBgfxHudRect &row, const MapBgfxHudRect &ramp,
                        const LegendRamp &content, bool uniform, const Palette &palette);
    int rampLabelCount(MapBgfxHudTextCache *cache, const LegendRamp &content,
                       double ramp_width) const;
    void drawHoverSwatch(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                         const Palette &palette);
    void comboPopup(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                    const FrameInputs &inputs, const Palette &palette);
    void paletteMenu(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                     const FrameInputs &inputs, const Palette &palette);
    void tooltipBox(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                    const FrameInputs &inputs, const Palette &palette);
    // Ramp colour at fraction 0..1 of a ramp (interpolated between stops).
    static QRgb rampColor(const LegendRamp &ramp, double fraction);
    QString rampTooltip(const LegendRamp &ramp) const;
    void openCombo(int group);
    void openPaletteMenu(int group);
    // Scroll-bar drag: puts the thumb's grab point at y.
    void dragComboThumbTo(double y);
    // Track click outside the thumb: one page towards y (QScrollBar).
    void pageComboTowards(double y);
    void addHitBox(const MapBgfxHudRect &rect, const Target &target, bool enabled,
                   Cursor cursor, const QString &tooltip = QString());
    // Whether a hit box of the last build still answers input (popup boxes
    // only while their popup is open).
    bool hitBoxLive(const HitBox &box) const;
    const HitBox *hitBoxAt(double x, double y) const;
    Target targetAt(double x, double y) const;
    static bool isPopupElement(Element element);
    // Elements whose press arms a click (release on the same target acts).
    static bool isClickable(Element element);
    static Action actionFor(Element element);
    Command commandFor(const Target &target);

    QFont font;
    QFont bold_font;
    QFont scale_font;
    QFont legend_label_font;
    QFont legend_value_font;
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
    bool view_switch_visible = true;
    bool fullscreen_available = false;
    bool fullscreen_active = false;

    // Legend content and its per-frame layout (positions of the last build,
    // used to anchor popups and the hover swatch).
    Legend legend_content;
    MapBgfxHudRect legend_rect;
    std::array<MapBgfxHudRect, 3> legend_combo_rects;
    std::array<MapBgfxHudRect, 3> legend_ramp_rects;
    // Popups: group whose combo list / palette menu is open, or -1.
    int open_combo = -1;
    int open_palette_menu = -1;
    // First visible row of the open combo list.
    int combo_scroll = 0;
    // A freshly opened list centres its current entry at the next build
    // (only then is the number of visible rows known).
    bool combo_center_pending = false;
    // Geometry of the open combo list (last build) for scroll-bar input.
    MapBgfxHudRect combo_list_rect;
    MapBgfxHudRect combo_track_rect;
    MapBgfxHudRect combo_thumb_rect;
    int combo_visible_rows = 0;
    // Distance from the thumb's top to where it was grabbed.
    double combo_thumb_grab = 0.0;
    // Wheel delta not yet turned into whole rows (touchpads).
    double combo_wheel_remainder = 0.0;

    QVector<HitBox> hit_boxes;
    QVector<MapBgfxHudRect> blocking_panels;
    Target hovered;
    Target armed;
    bool press_consumed = false;
    double pointer_x = -1.0;
    double pointer_y = -1.0;
    bool pointer_inside = false;
    // Tooltip of the hovered target: since when (frame time, -1 = start at
    // the next build), and whether a press dismissed it until the pointer
    // reaches another target.
    double tooltip_since_s = -1.0;
    bool tooltip_suppressed = false;
};

#endif // MAP_BGFX_HUD_H
