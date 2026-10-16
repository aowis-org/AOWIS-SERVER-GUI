#include "map/bgfx/map_bgfx_hud.h"

#include <QFontMetricsF>
#include <QLocale>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double PanelRadiusPx = 4.0;
constexpr double ButtonRadiusPx = 3.0;
constexpr double PanelSpacingPx = 8.0;
// Download panel (QWidget HUD: margins 8,5,6,5, spacing 7, busy bar 54x10,
// label minimum width 82).
constexpr double DownloadMarginLeftPx = 8.0;
constexpr double DownloadMarginRightPx = 6.0;
constexpr double DownloadMarginVerticalPx = 5.0;
constexpr double DownloadSpacingPx = 7.0;
constexpr double BusyBarWidthPx = 54.0;
constexpr double BusyBarHeightPx = 10.0;
constexpr double DownloadLabelMinimumWidthPx = 82.0;
// Buttons: text plus padding, like a compact QPushButton.
constexpr double ButtonPaddingHorizontalPx = 10.0;
constexpr double ButtonPaddingVerticalPx = 3.0;
// View switch frame (HudMarginPx 6 in map_monitor_hud_controls.cpp).
constexpr double FramePaddingPx = 6.0;
constexpr double SegmentMinimumWidthPx = 36.0;
// Fullscreen button: (TopControlBarHeight 62 - 8) / 2 = 27 px, padding 4.
constexpr double FullscreenButtonExtentPx = 27.0;
constexpr double FullscreenPaddingPx = 4.0;
// Scale label font (QWidget HUD: pixel size 11).
constexpr int ScaleLabelPixelSize = 11;

// Legend combo (a compact QComboBox): text inset, arrow area on the right.
constexpr double ComboTextPaddingPx = 6.0;
constexpr double ComboArrowAreaPx = 20.0;
constexpr double ComboArrowHalfWidthPx = 4.0;
constexpr double ComboArrowHalfHeightPx = 2.0;
constexpr double ComboPaddingVerticalPx = 4.0;
// Combo list rows (QComboBox popup item view).
constexpr double ComboItemPaddingHorizontalPx = 6.0;
constexpr double ComboItemPaddingVerticalPx = 3.0;
// Scroll bar of an overflowing combo list (a slim QScrollBar): track with a
// separator line, rounded thumb inset 2 px, at least 24 px long.
constexpr double ComboScrollBarWidthPx = 12.0;
constexpr double ComboScrollThumbInsetPx = 2.0;
constexpr double ComboScrollThumbMinimumPx = 24.0;
// One wheel notch (120) scrolls QApplication::wheelScrollLines() = 3 rows.
constexpr double ComboWheelDeltaPerRow = 40.0;
constexpr double PopupFramePx = 1.0;
// Ramp (MapSymbologyRampWidget): corner radius, ticks, label spacing.
constexpr double RampRadiusPx = 4.0;
constexpr double RampTickGapPx = 2.0;
constexpr double RampTickLengthPx = 4.0;
constexpr double RampLabelGapPx = 2.0;
constexpr double SwatchRadiusPx = 4.0;
constexpr double SwatchGapPx = 4.0;
constexpr double SwatchEdgePx = 2.0;
// Palette menu (EntityMapLegendHud::showPaletteMenu()): rows with margins
// 6,3,6,3 and spacing 8, a 124 px preview button with a 112 x 16 gradient
// icon, a "Flip" button; Sequential / Diverging sections, separator, Reset.
constexpr double MenuPaddingPx = 4.0;
constexpr double MenuRowMarginHorizontalPx = 6.0;
constexpr double MenuRowMarginVerticalPx = 3.0;
constexpr double MenuRowSpacingPx = 8.0;
constexpr double MenuLabelExtraWidthPx = 8.0;
constexpr double PalettePreviewWidthPx = 124.0;
#ifdef Q_OS_WASM
// WasmPopupMenu rows are taller (touch friendly).
constexpr double PalettePreviewHeightPx = 36.0;
#else
constexpr double PalettePreviewHeightPx = 26.0;
#endif
constexpr double PaletteIconWidthPx = 112.0;
constexpr double PaletteIconHeightPx = 16.0;
constexpr double MenuSectionPaddingPx = 6.0;
constexpr double MenuSeparatorHeightPx = 7.0;
constexpr double MenuActionPaddingPx = 8.0;
// Tooltip (QToolTip): text inset, placement below-right of the pointer.
constexpr double TooltipPaddingPx = 4.0;
constexpr double TooltipOffsetXPx = 2.0;
constexpr double TooltipOffsetYPx = 20.0;
constexpr double ViewportEdgePx = 2.0;

QRgb withAlpha(QRgb color, int alpha)
{
    return qRgba(qRed(color), qGreen(color), qBlue(color), alpha);
}

QRgb lighter(QRgb color, int factor)
{
    return QColor::fromRgba(color).lighter(factor).rgba();
}

QRgb darker(QRgb color, int factor)
{
    return QColor::fromRgba(color).darker(factor).rgba();
}

double snap(double value, double ratio)
{
    return std::round(value * ratio) / ratio;
}

// Text run with its layout origin at (x, y), snapped to the physical grid.
void drawText(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache, double x, double y,
              const QString &text, const QFont &font, QRgb color)
{
    if (text.isEmpty())
        return;
    const double ratio = cache->devicePixelRatio();
    canvas->sprite(snap(x, ratio), snap(y, ratio), cache->text(text, font), color);
}

// Normalized Web Mercator world -> WGS84 degrees (inverse of worldFromWgs84).
void wgs84FromWorld(double world_x, double world_y, double *longitude_deg, double *latitude_deg)
{
    *longitude_deg = world_x * 360.0 - 180.0;
    *latitude_deg = std::atan(std::sinh(MapBgfx2d::Pi * (1.0 - 2.0 * world_y)))
        / MapBgfx2d::DegreesToRadians;
}

bool rampFinite(const MapBgfxHud::LegendRamp &ramp)
{
    return std::isfinite(ramp.minimum) && std::isfinite(ramp.maximum);
}

// MapSymbologyRampWidget: "uniform" when min and max are (fuzzy) equal.
bool rampUniform(const MapBgfxHud::LegendRamp &ramp)
{
    return rampFinite(ramp) && qFuzzyCompare(ramp.minimum + 1.0, ramp.maximum + 1.0);
}
}

MapBgfxHud::Palette MapBgfxHud::Palette::fromQPalette(const QPalette &palette)
{
    Palette result;
    result.panel = withAlpha(palette.color(QPalette::Window).rgb(), 220);
    result.border = palette.color(QPalette::Mid).rgb();
    result.text = palette.color(QPalette::Text).rgb();
    result.button = palette.color(QPalette::Button).rgb();
    result.button_text = palette.color(QPalette::ButtonText).rgb();
    result.disabled_text = palette.color(QPalette::Disabled, QPalette::ButtonText).rgb();
    result.highlight = palette.color(QPalette::Highlight).rgb();
    result.highlighted_text = palette.color(QPalette::HighlightedText).rgb();
    result.window = palette.color(QPalette::Window).rgb();
    result.base = palette.color(QPalette::Base).rgb();
    result.alternate_base = palette.color(QPalette::AlternateBase).rgb();
    result.tooltip = palette.color(QPalette::ToolTipBase).rgb();
    result.tooltip_text = palette.color(QPalette::ToolTipText).rgb();
    return result;
}

void MapBgfxHud::setFont(const QFont &font)
{
    this->font = font;
    this->bold_font = font;
    this->bold_font.setBold(true);
    this->scale_font = font;
    this->scale_font.setPixelSize(ScaleLabelPixelSize);
    // MapSymbologyRampWidget: tick labels 3 pt smaller (at least 6 pt), the
    // hover value bold and 1 pt larger.
    this->legend_label_font = font;
    this->legend_value_font = this->bold_font;
    if (font.pointSizeF() > 0.0)
    {
        this->legend_label_font.setPointSizeF(std::max(6.0, font.pointSizeF() - 3.0));
        this->legend_value_font.setPointSizeF(font.pointSizeF() + 1.0);
    }
    else if (font.pixelSize() > 0)
    {
        // Pixel-sized application font: 3 pt are 4 px at 96 dpi.
        this->legend_label_font.setPixelSize(std::max(8, font.pixelSize() - 4));
        this->legend_value_font.setPixelSize(font.pixelSize() + 1);
    }
}

void MapBgfxHud::setImages(const QImage &crosshair, const QImage &fullscreen,
                           const QImage &fullscreen_exit)
{
    this->crosshair_image = crosshair;
    this->fullscreen_image = fullscreen;
    this->fullscreen_exit_image = fullscreen_exit;
}

void MapBgfxHud::setGps(bool valid, double longitude_deg, double latitude_deg)
{
    this->gps_valid = valid && std::isfinite(longitude_deg) && std::isfinite(latitude_deg);
    this->gps_longitude_deg = longitude_deg;
    this->gps_latitude_deg = latitude_deg;
}

void MapBgfxHud::setDownloadActivity(int tiles_active, int tiles_queued,
                                     int terrain_active, int terrain_queued)
{
    this->tiles_active = std::max(0, tiles_active);
    this->tiles_queued = std::max(0, tiles_queued);
    this->terrain_active = std::max(0, terrain_active);
    this->terrain_queued = std::max(0, terrain_queued);
    // Fresh numbers re-enable a clicked Cancel (QWidget HUD: updatePanel()).
    this->tiles_cancel_pending = false;
    this->terrain_cancel_pending = false;
}

void MapBgfxHud::setViewMode(bool two_d, bool globe_available)
{
    this->two_d = two_d;
    this->globe_available = globe_available;
}

void MapBgfxHud::setFullscreen(bool available, bool active)
{
    this->fullscreen_available = available;
    this->fullscreen_active = active;
}

void MapBgfxHud::setLegend(const Legend &legend)
{
    this->legend_content = legend;
    for (LegendGroup &group : this->legend_content.groups)
    {
        // Items and values are parallel lists; never index past either.
        const qsizetype count = std::min(group.items.size(), group.values.size());
        group.items.resize(count);
        group.values.resize(count);
        group.current = count > 0 ? std::clamp(group.current, 0, int(count) - 1) : 0;
    }
    if (!this->legend_content.visible)
    {
        closePopup();
        return;
    }
    // The palette menu belongs to a ramp: it closes when the ramp goes.
    if (this->open_palette_menu >= 0
        && !this->legend_content.groups[std::size_t(this->open_palette_menu)].ramp.visible)
    {
        this->open_palette_menu = -1;
    }
}

void MapBgfxHud::build(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                       const FrameInputs &inputs, const Palette &palette)
{
    cache->beginFrame();
    canvas->clear();
    buildOnce(canvas, cache, inputs, palette);
    if (cache->evictedSinceFrameStart())
    {
        // The atlas was cleared mid-build: rebuild against the refilled atlas.
        cache->beginFrame();
        canvas->clear();
        buildOnce(canvas, cache, inputs, palette);
    }
}

double MapBgfxHud::buttonHeight(MapBgfxHudTextCache *cache) const
{
    return std::ceil(cache->measure(QStringLiteral("Ag"), this->font).height()
                     + 2.0 * ButtonPaddingVerticalPx);
}

double MapBgfxHud::buttonWidth(MapBgfxHudTextCache *cache, const QString &label) const
{
    return std::ceil(cache->measure(label, this->font).width() + 2.0 * ButtonPaddingHorizontalPx);
}

void MapBgfxHud::addHitBox(const MapBgfxHudRect &rect, const Target &target, bool enabled,
                           Cursor cursor, const QString &tooltip)
{
    HitBox box;
    box.rect = rect;
    box.target = target;
    box.enabled = enabled;
    box.cursor = cursor;
    box.tooltip = tooltip;
    this->hit_boxes.append(box);
}

void MapBgfxHud::drawPanel(MapBgfxHudCanvas *canvas, const MapBgfxHudRect &rect,
                           const Palette &palette)
{
    canvas->fillRoundedRect(rect, PanelRadiusPx, palette.panel);
    canvas->strokeRoundedRect(rect, PanelRadiusPx, 1.0, palette.border);
    this->blocking_panels.append(rect);
}

void MapBgfxHud::drawButton(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                            const MapBgfxHudRect &rect, const QString &label, const Target &target,
                            bool enabled, bool checked, const Palette &palette,
                            const QString &tooltip)
{
    QRgb fill = palette.button;
    QRgb text_color = enabled ? palette.button_text : palette.disabled_text;
    if (checked)
    {
        fill = palette.highlight;
        text_color = palette.highlighted_text;
    }
    else if (enabled && this->armed == target && this->hovered == target)
    {
        fill = darker(palette.button, 112);
    }
    else if (enabled && this->hovered == target)
    {
        fill = lighter(palette.button, 106);
    }
    canvas->fillRoundedRect(rect, ButtonRadiusPx, fill);
    canvas->strokeRoundedRect(rect, ButtonRadiusPx, 1.0, palette.border);
    if (!label.isEmpty())
    {
        const QSizeF size = cache->measure(label, this->font);
        drawText(canvas, cache, rect.x + 0.5 * (rect.width - size.width()),
                 rect.y + 0.5 * (rect.height - size.height()), label, this->font, text_color);
    }
    addHitBox(rect, target, enabled, Cursor::PointingHand, tooltip);
}

double MapBgfxHud::downloadPanel(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                                 double x, double y, const QString &title, int active,
                                 int queued, Element cancel, bool cancel_pending, double time_s,
                                 const Palette &palette)
{
    const QString label = queued > 0
        ? QStringLiteral("%1 active · %2 queued").arg(active).arg(queued)
        : QStringLiteral("%1 active").arg(active);
    const QString cancel_text = QStringLiteral("Cancel");
    const QSizeF title_size = cache->measure(title, this->bold_font);
    const QSizeF label_size = cache->measure(label, this->font);
    const double label_width = std::max(DownloadLabelMinimumWidthPx, std::ceil(label_size.width()));
    const double cancel_width = buttonWidth(cache, cancel_text);
    const double content_height = std::max({title_size.height(), label_size.height(),
                                            BusyBarHeightPx, buttonHeight(cache)});
    const double width = std::ceil(DownloadMarginLeftPx + title_size.width() + DownloadSpacingPx
        + BusyBarWidthPx + DownloadSpacingPx + label_width + DownloadSpacingPx + cancel_width
        + DownloadMarginRightPx);
    if (canvas == nullptr)
        return width;

    const double height = std::ceil(content_height + 2.0 * DownloadMarginVerticalPx);
    const MapBgfxHudRect panel{x, y, width, height};
    drawPanel(canvas, panel, palette);
    const double ratio = cache->devicePixelRatio();

    double cursor = x + DownloadMarginLeftPx;
    drawText(canvas, cache, cursor, y + 0.5 * (height - title_size.height()), title,
             this->bold_font, palette.text);
    cursor += title_size.width() + DownloadSpacingPx;

    // Indeterminate busy bar (QProgressBar range 0..0): a chunk sweeping
    // back and forth over a dim track.
    const MapBgfxHudRect track{snap(cursor, ratio), snap(y + 0.5 * (height - BusyBarHeightPx), ratio),
                               BusyBarWidthPx, BusyBarHeightPx};
    canvas->fillRoundedRect(track, 2.0, withAlpha(palette.border, 110));
    const double chunk_width = 0.4 * BusyBarWidthPx;
    const double phase = std::fmod(std::max(0.0, time_s) * 0.8, 2.0);
    const double travel = phase < 1.0 ? phase : 2.0 - phase;
    canvas->fillRoundedRect(MapBgfxHudRect{track.x + travel * (BusyBarWidthPx - chunk_width),
                                           track.y, chunk_width, BusyBarHeightPx},
                            2.0, palette.highlight);
    cursor += BusyBarWidthPx + DownloadSpacingPx;

    drawText(canvas, cache, cursor, y + 0.5 * (height - label_size.height()), label, this->font,
             palette.text);
    cursor += label_width + DownloadSpacingPx;

    const double button_height = buttonHeight(cache);
    drawButton(canvas, cache,
               MapBgfxHudRect{snap(cursor, ratio), snap(y + 0.5 * (height - button_height), ratio),
                              cancel_width, button_height},
               cancel_text, Target{cancel, -1, -1}, !cancel_pending, false, palette);
    return width;
}

void MapBgfxHud::scaleBar(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                          const FrameInputs &inputs, const Palette &palette)
{
    if (!inputs.two_d || !this->scale_provider || !(inputs.camera.world_pixels > 0.0))
        return;
    const double viewport_width = inputs.viewport.width();
    const double viewport_height = inputs.viewport.height();
    // Sample like MapMonitorScaleHudWidget: a horizontal line of the maximum
    // bar width through the viewport centre, from the continuous camera.
    const double maximum_bar_width = ScaleWidthPx - 2.0 * ScalePaddingPx;
    const double sample_y = std::floor(0.5 * viewport_height);
    const double sample_x = std::max(0.0, std::floor(0.5 * (viewport_width - maximum_bar_width)));
    const MapBgfx2d::Camera &camera = inputs.camera;
    const std::function<void(double, double *, double *)> to_wgs84 =
        [&camera, viewport_width, viewport_height, sample_y](double screen_x, double *longitude,
                                                             double *latitude)
    {
        const double world_x = camera.center.x + (screen_x - 0.5 * viewport_width) / camera.world_pixels;
        const double world_y = camera.center.y + (sample_y - 0.5 * viewport_height) / camera.world_pixels;
        wgs84FromWorld(world_x, world_y, longitude, latitude);
    };
    double lon0 = 0.0;
    double lat0 = 0.0;
    double lon1 = 0.0;
    double lat1 = 0.0;
    to_wgs84(sample_x, &lon0, &lat0);
    to_wgs84(sample_x + maximum_bar_width, &lon1, &lat1);
    const ScaleBar scale = this->scale_provider(lon0, lat0, lon1, lat1, maximum_bar_width);
    if (!scale.valid || !(scale.width_px > 0.0))
        return;

    const MapBgfxHudRect panel{MarginPx, std::max(MarginPx, viewport_height - ScaleHeightPx - MarginPx),
                               ScaleWidthPx, ScaleHeightPx};
    canvas->fillRoundedRect(panel, PanelRadiusPx, palette.panel);
    canvas->strokeRoundedRect(panel, PanelRadiusPx, 1.0, palette.border);
    // Not blocking: the QWidget scale HUD is transparent for mouse events.

    const QSizeF label_size = cache->measure(scale.label, this->scale_font);
    drawText(canvas, cache, panel.x + 0.5 * (ScaleWidthPx - label_size.width()),
             panel.y + 2.0 + 0.5 * (18.0 - label_size.height()), scale.label, this->scale_font,
             palette.text);

    const double bar_width = std::clamp(std::round(scale.width_px), 1.0, maximum_bar_width);
    const double left = panel.x + std::floor(0.5 * (ScaleWidthPx - bar_width));
    const double right = left + bar_width;
    const double bottom = panel.y + ScaleHeightPx - ScalePaddingPx;
    const double top = bottom - ScaleTickHeightPx;
    canvas->line(left - 1.0, bottom, right + 1.0, bottom, 2.0, palette.text);
    canvas->line(left, top, left, bottom + 1.0, 2.0, palette.text);
    canvas->line(right, top, right, bottom + 1.0, 2.0, palette.text);
}

void MapBgfxHud::buildOnce(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                           const FrameInputs &inputs, const Palette &palette)
{
    this->hit_boxes.clear();
    this->blocking_panels.clear();
    canvas->setWhiteTexel(cache->whiteU(), cache->whiteV());
    const double viewport_width = inputs.viewport.width();
    const double viewport_height = inputs.viewport.height();
    const double ratio = cache->devicePixelRatio();

    // Crosshair (MapRenderHudWidget: :/icon/crosshair.png, 60x60 keep aspect).
    if (!this->crosshair_image.isNull())
    {
        QSizeF size = QSizeF(this->crosshair_image.size());
        size.scale(CrosshairExtentPx, CrosshairExtentPx, Qt::KeepAspectRatio);
        size = QSizeF(std::round(size.width()), std::round(size.height()));
        const MapBgfxHudSprite crosshair =
            cache->image(QStringLiteral("crosshair"), this->crosshair_image, size);
        canvas->sprite(std::floor(0.5 * (viewport_width - size.width())),
                       std::floor(0.5 * (viewport_height - size.height())), crosshair,
                       0xffffffffu);
    }

    // GPS position: red dot with a black outline (MapRenderHudWidget), at the
    // continuous camera's projection.
    if (inputs.two_d && this->gps_valid && inputs.camera.world_pixels > 0.0)
    {
        const MapBgfx2d::ScreenPoint gps = MapBgfx2d::screenFromWorld(
            inputs.camera, MapBgfx2d::worldFromWgs84(this->gps_longitude_deg,
                                                     this->gps_latitude_deg));
        canvas->fillCircle(gps.x, gps.y, GpsRadiusPx, qRgb(255, 0, 0));
        canvas->strokeCircle(gps.x, gps.y, GpsRadiusPx + 0.5, 1.0, qRgb(0, 0, 0));
    }

    scaleBar(canvas, cache, inputs, palette);

    // Download activity, top centre: one panel per busy source.
    {
        const bool tiles_busy = this->tiles_active > 0 || this->tiles_queued > 0;
        const bool terrain_busy = this->terrain_active > 0 || this->terrain_queued > 0;
        const double tiles_width = tiles_busy
            ? downloadPanel(nullptr, cache, 0.0, 0.0, QStringLiteral("Map tiles"),
                            this->tiles_active, this->tiles_queued, Element::CancelMapTiles,
                            this->tiles_cancel_pending, inputs.time_s, palette)
            : 0.0;
        const double terrain_width = terrain_busy
            ? downloadPanel(nullptr, cache, 0.0, 0.0, QStringLiteral("Terrain"),
                            this->terrain_active, this->terrain_queued, Element::CancelTerrain,
                            this->terrain_cancel_pending, inputs.time_s, palette)
            : 0.0;
        const double total = tiles_width + terrain_width
            + (tiles_busy && terrain_busy ? PanelSpacingPx : 0.0);
        double x = std::max(MarginPx, std::floor(0.5 * (viewport_width - total)));
        if (tiles_busy)
        {
            downloadPanel(canvas, cache, x, MarginPx, QStringLiteral("Map tiles"),
                          this->tiles_active, this->tiles_queued, Element::CancelMapTiles,
                          this->tiles_cancel_pending, inputs.time_s, palette);
            x += tiles_width + PanelSpacingPx;
        }
        if (terrain_busy)
        {
            downloadPanel(canvas, cache, x, MarginPx, QStringLiteral("Terrain"),
                          this->terrain_active, this->terrain_queued, Element::CancelTerrain,
                          this->terrain_cancel_pending, inputs.time_s, palette);
        }
    }

    // 2D / 3D switch, top left. 3D stays disabled until the bgfx globe exists.
    if (this->view_switch_visible)
    {
        const QString label_2d = QStringLiteral("2D");
        const QString label_3d = QStringLiteral("3D");
        const double segment_height = buttonHeight(cache);
        const double width_2d = std::max(SegmentMinimumWidthPx, buttonWidth(cache, label_2d));
        const double width_3d = std::max(SegmentMinimumWidthPx, buttonWidth(cache, label_3d));
        const MapBgfxHudRect frame{MarginPx, MarginPx,
                                   2.0 * FramePaddingPx + width_2d + width_3d,
                                   2.0 * FramePaddingPx + segment_height};
        drawPanel(canvas, frame, palette);
        drawButton(canvas, cache,
                   MapBgfxHudRect{frame.x + FramePaddingPx, frame.y + FramePaddingPx,
                                  width_2d, segment_height},
                   label_2d, Target{Element::ViewMode2d, -1, -1}, true, this->two_d, palette);
        drawButton(canvas, cache,
                   MapBgfxHudRect{frame.x + FramePaddingPx + width_2d, frame.y + FramePaddingPx,
                                  width_3d, segment_height},
                   label_3d, Target{Element::ViewMode3d, -1, -1}, this->globe_available,
                   !this->two_d, palette);
    }

    // Fullscreen, top right (desktop only).
    if (this->fullscreen_available)
    {
        const double frame_extent = FullscreenButtonExtentPx + 2.0 * FullscreenPaddingPx;
        const MapBgfxHudRect frame{std::max(MarginPx, viewport_width - frame_extent - MarginPx),
                                   MarginPx, frame_extent, frame_extent};
        drawPanel(canvas, frame, palette);
        const MapBgfxHudRect button{frame.x + FullscreenPaddingPx, frame.y + FullscreenPaddingPx,
                                    FullscreenButtonExtentPx, FullscreenButtonExtentPx};
        drawButton(canvas, cache, button, QString(), Target{Element::Fullscreen, -1, -1}, true,
                   false, palette);
        const QImage &icon = this->fullscreen_active ? this->fullscreen_exit_image
                                                     : this->fullscreen_image;
        if (!icon.isNull())
        {
            const double icon_extent = FullscreenButtonExtentPx - 2.0;
            const MapBgfxHudSprite sprite = cache->image(
                this->fullscreen_active ? QStringLiteral("fullscreen-exit")
                                        : QStringLiteral("fullscreen"),
                icon, QSizeF(icon_extent, icon_extent));
            canvas->sprite(snap(button.x + 1.0, ratio), snap(button.y + 1.0, ratio), sprite,
                           0xffffffffu);
        }
    }

    // Map legend, bottom right (slice 6b), then its popups and the tooltip
    // on top of everything.
    legendPanel(canvas, cache, inputs, palette);
    comboPopup(canvas, cache, inputs, palette);
    paletteMenu(canvas, cache, inputs, palette);
    tooltipBox(canvas, cache, inputs, palette);
}

// --- Legend ---------------------------------------------------------------------

QString MapBgfxHud::formatLegendValue(double value)
{
    // MapSymbologyRampWidget::formatValue().
    if (!std::isfinite(value))
        return QStringLiteral("—");

    const QLocale locale;
    const double absolute_value = std::abs(value);
    if (absolute_value == 0.0)
        return locale.toString(0.0, 'f', 2);

    const int decimal_exponent = static_cast<int>(std::floor(std::log10(absolute_value)));
    if (decimal_exponent >= 3 || decimal_exponent <= -3)
        return locale.toString(value, 'e', 2);

    const int decimal_places = std::max(0, 2 - decimal_exponent);
    const double decimal_factor = std::pow(10.0, decimal_places);
    const double rounded_value = std::round(value * decimal_factor) / decimal_factor;
    if (std::abs(rounded_value) >= 1000.0)
        return locale.toString(value, 'e', 2);

    return locale.toString(value, 'f', decimal_places);
}

QRgb MapBgfxHud::rampColor(const LegendRamp &ramp, double fraction)
{
    // networkSymbologyInterpolatedRampColor() on stops that already carry
    // the flip.
    if (ramp.colors.isEmpty())
        return qRgb(128, 128, 128);
    const int count = int(ramp.colors.size());
    const double scaled = std::clamp(fraction, 0.0, 1.0) * double(count - 1);
    const int left_index = std::min(count - 1, int(std::floor(scaled)));
    const int right_index = std::min(count - 1, left_index + 1);
    const double ratio = scaled - double(left_index);
    const QRgb interpolated = MapBgfxHudCanvas::interpolateArgb(
        ramp.colors.at(left_index), ramp.colors.at(right_index), ratio);
    return qRgb(qRed(interpolated), qGreen(interpolated), qBlue(interpolated));
}

QString MapBgfxHud::rampTooltip(const LegendRamp &ramp) const
{
    // MapSymbologyRampWidget::updateToolTip().
    QString palette_name;
    for (const LegendPaletteChoice &choice : this->legend_content.palettes)
    {
        if (choice.value == ramp.palette)
            palette_name = choice.name;
    }
    const QString unit_suffix = ramp.unit.isEmpty()
        ? QString()
        : QStringLiteral(" %1").arg(ramp.unit);
    const QString palette_text = ramp.flipped
        ? QStringLiteral("%1 · flipped").arg(palette_name)
        : palette_name;
    return QStringLiteral("Minimum: %1%3\nMaximum: %2%3\nColors: %4\nClick to choose colors")
        .arg(formatLegendValue(ramp.minimum), formatLegendValue(ramp.maximum), unit_suffix,
             palette_text);
}

double MapBgfxHud::comboHeight(MapBgfxHudTextCache *cache) const
{
    return std::ceil(cache->measure(QStringLiteral("Ag"), this->font).height()
                     + 2.0 * ComboPaddingVerticalPx);
}

double MapBgfxHud::comboItemHeight(MapBgfxHudTextCache *cache) const
{
    return std::ceil(cache->measure(QStringLiteral("Ag"), this->font).height()
                     + 2.0 * ComboItemPaddingVerticalPx);
}

void MapBgfxHud::legendPanel(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                             const FrameInputs &inputs, const Palette &palette)
{
    this->legend_rect = MapBgfxHudRect();
    this->legend_combo_rects.fill(MapBgfxHudRect());
    this->legend_ramp_rects.fill(MapBgfxHudRect());
    if (!this->legend_content.visible)
        return;

    const double viewport_width = inputs.viewport.width();
    const double viewport_height = inputs.viewport.height();
    // Fixed width like the QWidget HUD; narrower only when the map is.
    const double width = std::min(LegendWidthPx, viewport_width - 2.0 * MarginPx);
    if (width < 2.0 * LegendPaddingPx + 2.0 * ComboArrowAreaPx)
        return;
    const double combo_height = comboHeight(cache);
    double content_height = 0.0;
    int rows = 0;
    for (const LegendGroup &group : this->legend_content.groups)
    {
        content_height += combo_height;
        ++rows;
        if (group.ramp.visible)
        {
            content_height += LegendRampRowHeightPx;
            ++rows;
        }
    }
    const double height = 2.0 * LegendPaddingPx + content_height
        + LegendSpacingPx * double(std::max(0, rows - 1));
    // Bottom right (positionDesktopHudWidgets()).
    const MapBgfxHudRect panel{std::max(MarginPx, viewport_width - width - MarginPx),
                               std::max(MarginPx, viewport_height - height - MarginPx),
                               width, height};
    drawPanel(canvas, panel, palette);
    this->legend_rect = panel;

    const double inner_x = panel.x + LegendPaddingPx;
    const double inner_width = width - 2.0 * LegendPaddingPx;
    double y = panel.y + LegendPaddingPx;
    for (int group = 0; group < LegendGroupCount; ++group)
    {
        if (group > 0)
            y += LegendSpacingPx;
        const MapBgfxHudRect combo{inner_x, y, inner_width, combo_height};
        this->legend_combo_rects[std::size_t(group)] = combo;
        drawCombo(canvas, cache, combo, group, palette);
        y += combo_height;
        if (this->legend_content.groups[std::size_t(group)].ramp.visible)
        {
            y += LegendSpacingPx;
            drawRampRow(canvas, cache, MapBgfxHudRect{inner_x, y, inner_width,
                                                      LegendRampRowHeightPx},
                        group, palette);
            y += LegendRampRowHeightPx;
        }
    }
    drawHoverSwatch(canvas, cache, palette);
}

void MapBgfxHud::drawCombo(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                           const MapBgfxHudRect &rect, int group, const Palette &palette)
{
    const LegendGroup &content = this->legend_content.groups[std::size_t(group)];
    const Target target{Element::LegendCombo, group, -1};
    const bool open = this->open_combo == group;
    QRgb fill = palette.button;
    if (open)
        fill = darker(palette.button, 106);
    else if (this->hovered == target)
        fill = lighter(palette.button, 106);
    canvas->fillRoundedRect(rect, ButtonRadiusPx, fill);
    canvas->strokeRoundedRect(rect, ButtonRadiusPx, 1.0, open ? palette.highlight : palette.border);

    // Current item, elided like QComboBox's label.
    const QString text = content.items.value(content.current);
    const double available = rect.width - ComboTextPaddingPx - ComboArrowAreaPx;
    const QString shown = QFontMetricsF(this->font).elidedText(text, Qt::ElideRight, available);
    const QSizeF size = cache->measure(shown, this->font);
    drawText(canvas, cache, rect.x + ComboTextPaddingPx, rect.y + 0.5 * (rect.height - size.height()),
             shown, this->font, palette.button_text);

    // Drop-down arrow.
    const double center_x = rect.right() - 0.5 * ComboArrowAreaPx;
    const double center_y = rect.y + 0.5 * rect.height;
    canvas->fillTriangle(MapBgfxHudPoint{center_x - ComboArrowHalfWidthPx, center_y - ComboArrowHalfHeightPx},
                         MapBgfxHudPoint{center_x + ComboArrowHalfWidthPx, center_y - ComboArrowHalfHeightPx},
                         MapBgfxHudPoint{center_x, center_y + ComboArrowHalfHeightPx},
                         palette.button_text);
    addHitBox(rect, target, true, Cursor::PointingHand);
}

void MapBgfxHud::drawRampRow(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                             const MapBgfxHudRect &row, int group, const Palette &palette)
{
    const LegendRamp &ramp = this->legend_content.groups[std::size_t(group)].ramp;
    const MapBgfxHudRect bar{row.x + LegendRampInsetXPx, row.y + LegendRampInsetYPx,
                             row.width - 2.0 * LegendRampInsetXPx, LegendRampHeightPx};
    this->legend_ramp_rects[std::size_t(group)] = bar;
    const bool finite = rampFinite(ramp);
    const bool uniform = rampUniform(ramp);
    const QString tooltip = rampTooltip(ramp);
    // The whole row carries the widget's tooltip; the bar is the click
    // target for the palette menu (cross cursor only with finite values).
    addHitBox(row, Target{Element::LegendRampRow, group, -1}, true, Cursor::Arrow, tooltip);
    addHitBox(bar, Target{Element::LegendRamp, group, -1}, true,
              finite ? Cursor::Cross : Cursor::Arrow, tooltip);

    if (!finite)
    {
        canvas->fillRoundedRect(bar, RampRadiusPx, palette.alternate_base);
        canvas->strokeRoundedRect(bar, RampRadiusPx, 1.0, palette.border);
        const QString text = QStringLiteral("No finite values");
        const QSizeF size = cache->measure(text, this->font);
        drawText(canvas, cache, row.x + 0.5 * (row.width - size.width()),
                 row.y + 0.5 * (row.height - size.height()), text, this->font, palette.text);
        return;
    }

    if (uniform || ramp.colors.size() < 2)
        canvas->fillRoundedRect(bar, RampRadiusPx, rampColor(ramp, 0.5));
    else
        canvas->fillHorizontalGradient(bar, RampRadiusPx, ramp.colors);
    canvas->strokeRoundedRect(bar, RampRadiusPx, 1.0, palette.border);
    drawRampLabels(canvas, cache, row, bar, ramp, uniform, palette);
}

int MapBgfxHud::rampLabelCount(MapBgfxHudTextCache *cache, const LegendRamp &ramp,
                               double ramp_width) const
{
    // MapSymbologyRampWidget::labelCountForWidth(): 7, 5 or 3 labels,
    // whichever is the densest set that does not collide.
    if (!rampFinite(ramp) || rampUniform(ramp))
        return 7;

    const double minimum_gap = std::max(6.0, cache->measure(QStringLiteral("  "),
                                                            this->legend_label_font).width());
    const std::array<int, 3> label_counts = {{7, 5, 3}};
    for (const int label_count : label_counts)
    {
        double previous_right = -minimum_gap;
        bool fits = true;
        for (int index = 0; index < label_count; ++index)
        {
            const double fraction = double(index) / double(label_count - 1);
            const double value = ramp.minimum + (ramp.maximum - ramp.minimum) * fraction;
            const double text_width =
                cache->measure(formatLegendValue(value), this->legend_label_font).width() + 2.0;
            const double label_center_x = ramp_width * fraction;
            double label_left = label_center_x - text_width * 0.5;
            double label_right = label_center_x + text_width * 0.5;
            if (index == 0)
            {
                label_left = 0.0;
                label_right = text_width;
            }
            else if (index + 1 == label_count)
            {
                label_left = ramp_width - text_width;
                label_right = ramp_width;
            }
            if (label_left < 0.0 || label_right > ramp_width
                || (index > 0 && label_left - previous_right < minimum_gap))
            {
                fits = false;
                break;
            }
            previous_right = label_right;
        }
        if (fits)
            return label_count;
    }
    return 3;
}

void MapBgfxHud::drawRampLabels(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                                const MapBgfxHudRect &row, const MapBgfxHudRect &ramp,
                                const LegendRamp &content, bool uniform, const Palette &palette)
{
    Q_UNUSED(row)
    // MapSymbologyRampWidget::drawTicksAndLabels(): 1 px ticks 2 px below
    // the ramp, labels 2 px below the ticks; first label left aligned, last
    // right aligned, the others centred on their tick.
    const double tick_top = ramp.bottom() + RampTickGapPx;
    const double tick_bottom = tick_top + RampTickLengthPx;
    const int label_count = rampLabelCount(cache, content, ramp.width);
    for (int index = 0; index < label_count; ++index)
    {
        const double fraction = double(index) / double(label_count - 1);
        const double x = ramp.x + ramp.width * fraction;
        canvas->line(x, tick_top, x, tick_bottom, 1.0, palette.text);
    }

    const double label_top = tick_bottom + RampLabelGapPx;
    if (uniform)
    {
        const QString text = QStringLiteral("%1 · uniform").arg(formatLegendValue(content.minimum));
        const QSizeF size = cache->measure(text, this->legend_label_font);
        drawText(canvas, cache, ramp.x + 0.5 * (ramp.width - size.width()), label_top, text,
                 this->legend_label_font, palette.text);
        return;
    }

    for (int index = 0; index < label_count; ++index)
    {
        const double fraction = double(index) / double(label_count - 1);
        const double value = content.minimum + (content.maximum - content.minimum) * fraction;
        const QString text = formatLegendValue(value);
        const double advance = cache->measure(text, this->legend_label_font).width();
        double x = ramp.x + ramp.width * fraction - 0.5 * advance;
        if (index == 0)
            x = ramp.x;
        else if (index + 1 == label_count)
            x = ramp.right() - advance;
        drawText(canvas, cache, x, label_top, text, this->legend_label_font, palette.text);
    }
}

void MapBgfxHud::drawHoverSwatch(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                                 const Palette &palette)
{
    // MapSymbologyHoverSwatch: while the pointer is over a finite ramp (and
    // nothing is pressed or popped up), a 96 x 52 chip above the pointer
    // with the colour and value at that position. It stays inside the
    // legend (the QWidget swatch is a child of the legend widget).
    if (this->hovered.element != Element::LegendRamp || popupOpen()
        || this->armed.element != Element::None || !this->pointer_inside)
    {
        return;
    }
    const int group = this->hovered.group;
    if (group < 0 || group >= LegendGroupCount)
        return;
    const LegendRamp &ramp = this->legend_content.groups[std::size_t(group)].ramp;
    const MapBgfxHudRect &bar = this->legend_ramp_rects[std::size_t(group)];
    if (!ramp.visible || !rampFinite(ramp) || !bar.contains(this->pointer_x, this->pointer_y))
        return;

    const bool uniform = rampUniform(ramp);
    const double fraction = std::clamp((this->pointer_x - bar.x) / bar.width, 0.0, 1.0);
    const double anchor_x = std::round(bar.x + bar.width * fraction);
    const double anchor_y = std::round(bar.y);
    const MapBgfxHudRect &legend = this->legend_rect;
    double x = anchor_x - std::floor(0.5 * LegendSwatchWidthPx);
    double y = anchor_y - LegendSwatchHeightPx - SwatchGapPx;
    x = std::clamp(x, legend.x + SwatchEdgePx,
                   std::max(legend.x + SwatchEdgePx,
                            legend.right() - LegendSwatchWidthPx - SwatchEdgePx));
    if (y < legend.y + SwatchEdgePx)
        y = anchor_y + std::round(bar.height) + SwatchGapPx;
    const MapBgfxHudRect swatch{x, y, LegendSwatchWidthPx, LegendSwatchHeightPx};

    const double value = uniform
        ? ramp.minimum
        : ramp.minimum + (ramp.maximum - ramp.minimum) * fraction;
    canvas->fillRoundedRect(swatch, SwatchRadiusPx, rampColor(ramp, uniform ? 0.5 : fraction));
    canvas->strokeRoundedRect(swatch, SwatchRadiusPx, 1.0, palette.border);
    const QString text = formatLegendValue(value);
    const QSizeF size = cache->measure(text, this->legend_value_font);
    const double text_x = swatch.x + 0.5 * (swatch.width - size.width());
    const double text_y = swatch.y + 0.5 * (swatch.height - size.height());
    // Dark shadow 1 px down-right, then white: readable on any ramp colour.
    drawText(canvas, cache, text_x + 1.0, text_y + 1.0, text, this->legend_value_font,
             qRgba(0, 0, 0, 180));
    drawText(canvas, cache, text_x, text_y, text, this->legend_value_font, qRgb(255, 255, 255));
}

void MapBgfxHud::comboPopup(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                            const FrameInputs &inputs, const Palette &palette)
{
    this->combo_list_rect = MapBgfxHudRect();
    this->combo_visible_rows = 0;
    if (this->open_combo < 0 || this->open_combo >= LegendGroupCount)
        return;
    const LegendGroup &content = this->legend_content.groups[std::size_t(this->open_combo)];
    const MapBgfxHudRect &combo = this->legend_combo_rects[std::size_t(this->open_combo)];
    const int count = int(content.items.size());
    if (count == 0 || !(combo.width > 0.0))
        return;

    // Drop-down below the combo, or above when it only fits there, tall
    // enough for every entry. Only when neither side has room for all of
    // them does the list take the larger side and scroll (with a scroll
    // bar).
    const double viewport_width = inputs.viewport.width();
    const double viewport_height = inputs.viewport.height();
    const double item_height = comboItemHeight(cache);
    int rows = count;
    double list_height = double(rows) * item_height + 2.0 * PopupFramePx;
    const double space_below = viewport_height - ViewportEdgePx - combo.bottom();
    const double space_above = combo.y - ViewportEdgePx;
    bool below = true;
    if (list_height > space_below)
    {
        if (list_height <= space_above)
        {
            below = false;
        }
        else
        {
            below = space_below >= space_above;
            const double space = std::max(space_below, space_above);
            rows = std::clamp(int(std::floor((space - 2.0 * PopupFramePx) / item_height)), 1, rows);
            list_height = double(rows) * item_height + 2.0 * PopupFramePx;
        }
    }
    const double x = std::max(ViewportEdgePx,
                              std::min(combo.x, viewport_width - ViewportEdgePx - combo.width));
    const MapBgfxHudRect list{x, below ? combo.bottom() : combo.y - list_height, combo.width,
                              list_height};
    this->combo_list_rect = list;
    this->combo_visible_rows = rows;
    if (this->combo_center_pending)
    {
        // Opened just now: the current entry in the middle where possible.
        this->combo_scroll = content.current - rows / 2;
        this->combo_center_pending = false;
    }
    this->combo_scroll = std::clamp(this->combo_scroll, 0, count - rows);

    canvas->fillRect(list, palette.base);
    canvas->strokeRoundedRect(list, 0.0, PopupFramePx, palette.border);
    addHitBox(list, Target{Element::Popup, this->open_combo, PopupComboList}, true, Cursor::Arrow);

    const bool scrollable = count > rows;
    const double scroll_width = scrollable ? ComboScrollBarWidthPx : 0.0;
    const bool hovering_item = this->hovered.element == Element::LegendComboItem;
    for (int row = 0; row < rows; ++row)
    {
        const int index = this->combo_scroll + row;
        const MapBgfxHudRect item{list.x + PopupFramePx,
                                  list.y + PopupFramePx + double(row) * item_height,
                                  list.width - 2.0 * PopupFramePx - scroll_width, item_height};
        const Target target{Element::LegendComboItem, this->open_combo, index};
        // The hovered row, or the current one while nothing is hovered.
        const bool highlighted = hovering_item ? this->hovered == target : index == content.current;
        if (highlighted)
            canvas->fillRect(item, palette.highlight);
        const QString shown = QFontMetricsF(this->font).elidedText(
            content.items.at(index), Qt::ElideRight,
            item.width - 2.0 * ComboItemPaddingHorizontalPx);
        const QSizeF size = cache->measure(shown, this->font);
        drawText(canvas, cache, item.x + ComboItemPaddingHorizontalPx,
                 item.y + 0.5 * (item.height - size.height()), shown, this->font,
                 highlighted ? palette.highlighted_text : palette.text);
        addHitBox(item, target, true, Cursor::PointingHand);
    }

    this->combo_track_rect = MapBgfxHudRect();
    this->combo_thumb_rect = MapBgfxHudRect();
    if (scrollable)
    {
        // Track (separated from the rows by a line) and a thumb whose length
        // is the visible share of the list, readable on light and dark
        // palettes: text colour at low alpha, stronger on hover, highlight
        // while dragged.
        const MapBgfxHudRect track{list.right() - PopupFramePx - scroll_width,
                                   list.y + PopupFramePx, scroll_width,
                                   list.height - 2.0 * PopupFramePx};
        canvas->fillRect(track, withAlpha(palette.text, 22));
        canvas->line(track.x + 0.5, track.y, track.x + 0.5, track.bottom(), 1.0,
                     withAlpha(palette.border, 160));
        const double inner_height = track.height - 2.0 * ComboScrollThumbInsetPx;
        const double thumb_height = std::min(inner_height, std::max(ComboScrollThumbMinimumPx,
            inner_height * double(rows) / double(count)));
        const double thumb_y = track.y + ComboScrollThumbInsetPx
            + (inner_height - thumb_height) * double(this->combo_scroll) / double(count - rows);
        const MapBgfxHudRect thumb{track.x + ComboScrollThumbInsetPx + 0.5, thumb_y,
                                   track.width - 2.0 * ComboScrollThumbInsetPx - 0.5,
                                   thumb_height};
        this->combo_track_rect = track;
        this->combo_thumb_rect = thumb;
        const Target target{Element::LegendComboScroll, this->open_combo, -1};
        QRgb thumb_color = withAlpha(palette.text, 95);
        if (this->armed == target)
            thumb_color = palette.highlight;
        else if (this->hovered == target && this->pointer_inside
                 && thumb.contains(this->pointer_x, this->pointer_y))
            thumb_color = withAlpha(palette.text, 150);
        canvas->fillRoundedRect(thumb, 0.5 * thumb.width, thumb_color);
        addHitBox(track, target, true, Cursor::Arrow);
    }
}

void MapBgfxHud::paletteMenu(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                             const FrameInputs &inputs, const Palette &palette)
{
    if (this->open_palette_menu < 0 || this->open_palette_menu >= LegendGroupCount)
        return;
    const int group = this->open_palette_menu;
    const LegendRamp &ramp = this->legend_content.groups[std::size_t(group)].ramp;
    const MapBgfxHudRect &bar = this->legend_ramp_rects[std::size_t(group)];
    const QVector<LegendPaletteChoice> &choices = this->legend_content.palettes;
    if (!(bar.width > 0.0) || choices.isEmpty())
        return;

    const double viewport_width = inputs.viewport.width();
    const double viewport_height = inputs.viewport.height();
    const double text_height = cache->measure(QStringLiteral("Ag"), this->font).height();
    double label_width = 0.0;
    int sections = 0;
    for (qsizetype index = 0; index < choices.size(); ++index)
    {
        label_width = std::max(label_width, cache->measure(choices.at(index).name, this->font).width());
        if (index == 0 || choices.at(index).diverging != choices.at(index - 1).diverging)
            ++sections;
    }
    label_width = std::ceil(label_width + MenuLabelExtraWidthPx);
    const QString flip_text = QStringLiteral("Flip");
    const double flip_width = buttonWidth(cache, flip_text);
    const double row_height = PalettePreviewHeightPx + 2.0 * MenuRowMarginVerticalPx;
    const double section_height = std::ceil(text_height + MenuSectionPaddingPx);
    const double action_height = std::ceil(text_height + MenuActionPaddingPx);
    const double width = 2.0 * MenuPaddingPx + 2.0 * MenuRowMarginHorizontalPx + label_width
        + MenuRowSpacingPx + PalettePreviewWidthPx + MenuRowSpacingPx + flip_width;
    const double height = 2.0 * MenuPaddingPx + double(sections) * section_height
        + double(choices.size()) * row_height + MenuSeparatorHeightPx + action_height;

    // QMenu::popup() at the ramp widget's left edge, 4 px below the ramp;
    // above the ramp when it does not fit below; always inside the map.
    const double anchor_x = bar.x - LegendRampInsetXPx;
    double x = std::min(anchor_x, viewport_width - ViewportEdgePx - width);
    x = std::max(ViewportEdgePx, x);
    double y = bar.bottom() + SwatchGapPx;
    if (y + height > viewport_height - ViewportEdgePx)
        y = bar.y - SwatchGapPx - height;
    y = std::max(ViewportEdgePx, std::min(y, viewport_height - ViewportEdgePx - height));
    const MapBgfxHudRect menu{x, y, width, height};
    canvas->fillRoundedRect(menu, 2.0, palette.window);
    canvas->strokeRoundedRect(menu, 2.0, 1.0, palette.border);
    addHitBox(menu, Target{Element::Popup, group, PopupPaletteMenu}, true, Cursor::Arrow);

    const double row_left = menu.x + MenuPaddingPx + MenuRowMarginHorizontalPx;
    const double row_right = menu.right() - MenuPaddingPx - MenuRowMarginHorizontalPx;
    double cursor_y = menu.y + MenuPaddingPx;
    for (qsizetype index = 0; index < choices.size(); ++index)
    {
        const LegendPaletteChoice &choice = choices.at(index);
        if (index == 0 || choice.diverging != choices.at(index - 1).diverging)
        {
            // Section header: title, then a rule to the right edge.
            const QString title = choice.diverging ? QStringLiteral("Diverging")
                                                   : QStringLiteral("Sequential");
            const QSizeF title_size = cache->measure(title, this->font);
            drawText(canvas, cache, row_left, cursor_y + 0.5 * (section_height - title_size.height()),
                     title, this->font, palette.disabled_text);
            const double rule_y = std::floor(cursor_y + 0.5 * section_height) + 0.5;
            const double rule_left = row_left + title_size.width() + MenuRowSpacingPx;
            if (rule_left < row_right)
                canvas->line(rule_left, rule_y, row_right, rule_y, 1.0, palette.border);
            cursor_y += section_height;
        }

        const QSizeF name_size = cache->measure(choice.name, this->font);
        drawText(canvas, cache, row_left, cursor_y + 0.5 * (row_height - name_size.height()),
                 choice.name, this->font, palette.text);

        const bool selected = ramp.palette == choice.value;
        const double button_y = cursor_y + MenuRowMarginVerticalPx;
        const MapBgfxHudRect flip{row_right - flip_width, button_y, flip_width,
                                  PalettePreviewHeightPx};
        const MapBgfxHudRect preview{flip.x - MenuRowSpacingPx - PalettePreviewWidthPx, button_y,
                                     PalettePreviewWidthPx, PalettePreviewHeightPx};
        drawButton(canvas, cache, preview, QString(),
                   Target{Element::LegendPalette, group, int(index)}, true, selected, palette,
                   QStringLiteral("Use %1 colors").arg(choice.name));
        // palettePreviewIcon(): the gradient with a translucent dark frame.
        const MapBgfxHudRect icon{preview.x + std::floor(0.5 * (preview.width - PaletteIconWidthPx)),
                                  preview.y + std::floor(0.5 * (preview.height - PaletteIconHeightPx)),
                                  PaletteIconWidthPx, PaletteIconHeightPx};
        canvas->fillHorizontalGradient(icon, 0.0, choice.colors);
        canvas->strokeRoundedRect(icon, 0.0, 1.0, qRgba(0, 0, 0, 120));
        drawButton(canvas, cache, flip, flip_text,
                   Target{Element::LegendPaletteFlip, group, int(index)}, true,
                   selected && ramp.flipped, palette, QStringLiteral("Flip color direction"));
        cursor_y += row_height;
    }

    // Separator, then the Reset action (QMenu action row).
    const double separator_y = std::floor(cursor_y + 0.5 * MenuSeparatorHeightPx) + 0.5;
    canvas->line(menu.x + MenuPaddingPx, separator_y, menu.right() - MenuPaddingPx, separator_y,
                 1.0, palette.border);
    cursor_y += MenuSeparatorHeightPx;
    const MapBgfxHudRect reset{menu.x + MenuPaddingPx, cursor_y,
                               menu.width - 2.0 * MenuPaddingPx, action_height};
    const Target reset_target{Element::LegendPaletteReset, group, -1};
    const bool reset_hovered = this->hovered == reset_target;
    if (reset_hovered)
        canvas->fillRect(reset, palette.highlight);
    const QString reset_text = QStringLiteral("Reset");
    const QSizeF reset_size = cache->measure(reset_text, this->font);
    drawText(canvas, cache, row_left, reset.y + 0.5 * (reset.height - reset_size.height()),
             reset_text, this->font, reset_hovered ? palette.highlighted_text : palette.text);
    addHitBox(reset, reset_target, true, Cursor::PointingHand);
}

void MapBgfxHud::tooltipBox(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                            const FrameInputs &inputs, const Palette &palette)
{
    if (!this->pointer_inside || this->tooltip_suppressed
        || this->armed.element != Element::None)
    {
        return;
    }
    const HitBox *box = hitBoxAt(this->pointer_x, this->pointer_y);
    if (box == nullptr || box->target != this->hovered || box->tooltip.isEmpty())
        return;
    // QToolTip's wake-up delay, measured in frame time from the first frame
    // that saw this target under a resting pointer.
    if (this->tooltip_since_s < 0.0)
        this->tooltip_since_s = inputs.time_s;
    if (inputs.time_s - this->tooltip_since_s < TooltipDelayS)
        return;

    const QStringList lines = box->tooltip.split(QLatin1Char('\n'));
    const double line_height = cache->measure(QStringLiteral("Ag"), this->font).height();
    double text_width = 0.0;
    for (const QString &line : lines)
        text_width = std::max(text_width, cache->measure(line, this->font).width());
    const double width = std::ceil(text_width + 2.0 * TooltipPaddingPx);
    const double height = std::ceil(double(lines.size()) * line_height + 2.0 * TooltipPaddingPx);
    const double viewport_width = inputs.viewport.width();
    const double viewport_height = inputs.viewport.height();
    double x = this->pointer_x + TooltipOffsetXPx;
    double y = this->pointer_y + TooltipOffsetYPx;
    if (x + width > viewport_width - ViewportEdgePx)
        x = std::max(ViewportEdgePx, viewport_width - ViewportEdgePx - width);
    if (y + height > viewport_height - ViewportEdgePx)
        y = std::max(ViewportEdgePx, this->pointer_y - height - SwatchGapPx);
    const MapBgfxHudRect tip{std::round(x), std::round(y), width, height};
    canvas->fillRect(tip, palette.tooltip);
    canvas->strokeRoundedRect(tip, 0.0, 1.0, palette.border);
    double line_y = tip.y + TooltipPaddingPx;
    for (const QString &line : lines)
    {
        drawText(canvas, cache, tip.x + TooltipPaddingPx, line_y, line, this->font,
                 palette.tooltip_text);
        line_y += line_height;
    }
}

// --- Input ------------------------------------------------------------------------

bool MapBgfxHud::hitBoxLive(const HitBox &box) const
{
    // Hit boxes come from the last build. A popup closed since then (click
    // on an entry, Escape, press outside) must not answer input before the
    // next frame, or a quick second click would land on a vanished entry.
    switch (box.target.element)
    {
        case Element::LegendComboItem:
        case Element::LegendComboScroll:
            return this->open_combo == box.target.group;
        case Element::LegendPalette:
        case Element::LegendPaletteFlip:
        case Element::LegendPaletteReset:
            return this->open_palette_menu == box.target.group;
        case Element::Popup:
            return box.target.index == PopupComboList ? this->open_combo == box.target.group
                                                      : this->open_palette_menu == box.target.group;
        default:
            return true;
    }
}

const MapBgfxHud::HitBox *MapBgfxHud::hitBoxAt(double x, double y) const
{
    // Topmost first: popups are added last.
    for (qsizetype index = this->hit_boxes.size() - 1; index >= 0; --index)
    {
        const HitBox &box = this->hit_boxes.at(index);
        if (box.enabled && box.rect.contains(x, y) && hitBoxLive(box))
            return &box;
    }
    return nullptr;
}

MapBgfxHud::Target MapBgfxHud::targetAt(double x, double y) const
{
    const HitBox *box = hitBoxAt(x, y);
    return box != nullptr ? box->target : Target();
}

bool MapBgfxHud::blocksInput(double x, double y) const
{
    for (const MapBgfxHudRect &panel : this->blocking_panels)
    {
        if (panel.contains(x, y))
            return true;
    }
    // Open popups block too, but only while they are still open (their
    // boxes are from the last build, see hitBoxLive()).
    for (const HitBox &box : this->hit_boxes)
    {
        if (box.target.element == Element::Popup && box.rect.contains(x, y) && hitBoxLive(box))
            return true;
    }
    return false;
}

MapBgfxHud::Cursor MapBgfxHud::cursor() const
{
    if (!this->pointer_inside || this->armed.element == Element::LegendComboScroll)
        return Cursor::Arrow;
    const HitBox *box = hitBoxAt(this->pointer_x, this->pointer_y);
    if (box == nullptr || box->target != this->hovered)
        return Cursor::Arrow;
    return box->cursor;
}

bool MapBgfxHud::isPopupElement(Element element)
{
    switch (element)
    {
        case Element::LegendComboItem:
        case Element::LegendComboScroll:
        case Element::LegendPalette:
        case Element::LegendPaletteFlip:
        case Element::LegendPaletteReset:
        case Element::Popup:
            return true;
        default:
            return false;
    }
}

bool MapBgfxHud::isClickable(Element element)
{
    switch (element)
    {
        case Element::CancelMapTiles:
        case Element::CancelTerrain:
        case Element::ViewMode2d:
        case Element::ViewMode3d:
        case Element::Fullscreen:
        case Element::LegendComboItem:
        case Element::LegendPalette:
        case Element::LegendPaletteFlip:
        case Element::LegendPaletteReset:
            return true;
        default:
            return false;
    }
}

MapBgfxHud::Action MapBgfxHud::actionFor(Element element)
{
    switch (element)
    {
        case Element::CancelMapTiles:
            return Action::CancelMapTiles;
        case Element::CancelTerrain:
            return Action::CancelTerrain;
        case Element::ViewMode2d:
            return Action::ViewMode2d;
        case Element::ViewMode3d:
            return Action::ViewMode3d;
        case Element::Fullscreen:
            return Action::ToggleFullscreen;
        case Element::LegendComboItem:
            return Action::LegendVisual;
        case Element::LegendPalette:
        case Element::LegendPaletteFlip:
        case Element::LegendPaletteReset:
            return Action::LegendPalette;
        default:
            break;
    }
    return Action::None;
}

MapBgfxHud::Command MapBgfxHud::commandFor(const Target &target)
{
    Command command;
    command.action = actionFor(target.element);
    command.group = target.group;
    if (target.group < 0 || target.group >= LegendGroupCount)
    {
        // Not a legend element (6a buttons): the action alone.
        command.group = -1;
        return command;
    }
    LegendGroup &group = this->legend_content.groups[std::size_t(target.group)];
    const QVector<LegendPaletteChoice> &choices = this->legend_content.palettes;
    switch (target.element)
    {
        case Element::LegendComboItem:
            if (target.index < 0 || target.index >= int(group.values.size()))
                return Command();
            // Shown at once; the application pushes the new range next.
            group.current = target.index;
            command.value = group.values.at(target.index);
            break;
        case Element::LegendPalette:
            if (target.index < 0 || target.index >= int(choices.size()))
                return Command();
            command.value = choices.at(target.index).value;
            command.flipped = false;
            break;
        case Element::LegendPaletteFlip:
            if (target.index < 0 || target.index >= int(choices.size()))
                return Command();
            command.value = choices.at(target.index).value;
            // Flip toggles the active palette; on another palette it selects
            // that palette flipped (QWidget menu).
            command.flipped = group.ramp.palette == command.value ? !group.ramp.flipped : true;
            break;
        case Element::LegendPaletteReset:
            command.value = group.ramp.default_palette;
            command.flipped = group.ramp.default_flipped;
            break;
        default:
            return Command();
    }
    if (command.action == Action::LegendPalette)
    {
        group.ramp.palette = command.value;
        group.ramp.flipped = command.flipped;
    }
    closePopup();
    return command;
}

void MapBgfxHud::openCombo(int group)
{
    this->open_palette_menu = -1;
    this->open_combo = group;
    // The current entry is centred once the build knows the row count.
    this->combo_scroll = 0;
    this->combo_center_pending = true;
    this->combo_wheel_remainder = 0.0;
}

void MapBgfxHud::openPaletteMenu(int group)
{
    this->open_combo = -1;
    this->open_palette_menu = group;
}

bool MapBgfxHud::closePopup()
{
    if (!popupOpen())
        return false;
    this->open_combo = -1;
    this->open_palette_menu = -1;
    this->combo_wheel_remainder = 0.0;
    if (isPopupElement(this->armed.element))
        this->armed = Target();
    return true;
}

void MapBgfxHud::dragComboThumbTo(double y)
{
    if (this->open_combo < 0 || this->combo_visible_rows <= 0)
        return;
    const int count = int(this->legend_content.groups[std::size_t(this->open_combo)].items.size());
    const int maximum = count - this->combo_visible_rows;
    const MapBgfxHudRect &track = this->combo_track_rect;
    const double travel = track.height - 2.0 * ComboScrollThumbInsetPx
        - this->combo_thumb_rect.height;
    if (maximum <= 0 || !(travel > 0.0))
        return;
    // The grabbed point of the thumb follows the pointer (QScrollBar).
    const double thumb_top = y - this->combo_thumb_grab;
    const double fraction = std::clamp(
        (thumb_top - track.y - ComboScrollThumbInsetPx) / travel, 0.0, 1.0);
    this->combo_scroll = std::clamp(int(std::lround(fraction * double(maximum))), 0, maximum);
}

void MapBgfxHud::pageComboTowards(double y)
{
    if (this->open_combo < 0 || this->combo_visible_rows <= 0)
        return;
    const int count = int(this->legend_content.groups[std::size_t(this->open_combo)].items.size());
    const int maximum = std::max(0, count - this->combo_visible_rows);
    // One page minus one row of overlap, like a QAbstractItemView.
    const int page = std::max(1, this->combo_visible_rows - 1);
    const int direction = y < this->combo_thumb_rect.y ? -1 : 1;
    this->combo_scroll = std::clamp(this->combo_scroll + direction * page, 0, maximum);
}

void MapBgfxHud::clearHover()
{
    this->hovered = Target();
    this->pointer_inside = false;
    this->tooltip_since_s = -1.0;
}

bool MapBgfxHud::mousePress(double x, double y)
{
    const Target target = targetAt(x, y);
    this->pointer_x = x;
    this->pointer_y = y;
    this->pointer_inside = true;
    this->hovered = target;
    this->armed = Target();
    // A press dismisses the tooltip until the pointer reaches another target.
    this->tooltip_suppressed = true;
    if (popupOpen() && !isPopupElement(target.element))
    {
        // Qt popups: a press outside closes the popup and is swallowed. A
        // press on the combo that owns the open list just closes it.
        closePopup();
        this->press_consumed = true;
        return true;
    }
    switch (target.element)
    {
        case Element::LegendCombo:
            // QComboBox opens its list on press.
            openCombo(target.group);
            this->press_consumed = true;
            return true;
        case Element::LegendRamp:
            // MapSymbologyRampWidget opens the palette menu on press.
            openPaletteMenu(target.group);
            this->press_consumed = true;
            return true;
        case Element::LegendComboScroll:
            if (this->combo_thumb_rect.contains(x, y))
            {
                // Grab the thumb where it was pressed; moves drag it.
                this->armed = target;
                this->combo_thumb_grab = y - this->combo_thumb_rect.y;
            }
            else
            {
                // Track outside the thumb: page towards the click.
                pageComboTowards(y);
            }
            this->press_consumed = true;
            return true;
        default:
            break;
    }
    if (isClickable(target.element))
        this->armed = target;
    this->press_consumed = this->armed.element != Element::None || blocksInput(x, y);
    return this->press_consumed;
}

MapBgfxHud::Action MapBgfxHud::mouseRelease(double x, double y, bool *consumed, Command *command)
{
    const bool was_consumed = this->press_consumed;
    const Target armed_target = this->armed;
    this->press_consumed = false;
    this->armed = Target();
    if (consumed != nullptr)
        *consumed = was_consumed;
    if (command != nullptr)
        *command = Command();
    if (armed_target.element == Element::None
        || armed_target.element == Element::LegendComboScroll
        || targetAt(x, y) != armed_target)
    {
        return Action::None;
    }
    // A clicked Cancel stays disabled until fresh activity numbers arrive.
    if (armed_target.element == Element::CancelMapTiles)
        this->tiles_cancel_pending = true;
    if (armed_target.element == Element::CancelTerrain)
        this->terrain_cancel_pending = true;
    const Command result = commandFor(armed_target);
    if (command != nullptr)
        *command = result;
    return result.action;
}

bool MapBgfxHud::mouseMove(double x, double y, bool *over_button)
{
    this->pointer_x = x;
    this->pointer_y = y;
    this->pointer_inside = true;
    if (this->armed.element == Element::LegendComboScroll)
        dragComboThumbTo(y);
    const Target target = targetAt(x, y);
    if (target != this->hovered)
    {
        this->hovered = target;
        this->tooltip_since_s = -1.0;
        this->tooltip_suppressed = false;
    }
    if (over_button != nullptr)
        *over_button = cursor() == Cursor::PointingHand;
    return this->press_consumed;
}

bool MapBgfxHud::mouseWheel(double x, double y, double angle_delta_y)
{
    if (this->open_combo >= 0 && this->combo_list_rect.contains(x, y))
    {
        const int count = int(this->legend_content.groups[std::size_t(this->open_combo)].items.size());
        const int maximum = std::max(0, count - this->combo_visible_rows);
        // Fine-grained (touchpad) deltas add up to whole rows.
        this->combo_wheel_remainder += angle_delta_y;
        const int rows = int(this->combo_wheel_remainder / ComboWheelDeltaPerRow);
        this->combo_wheel_remainder -= double(rows) * ComboWheelDeltaPerRow;
        this->combo_scroll = std::clamp(this->combo_scroll - rows, 0, maximum);
        return true;
    }
    // Over any HUD panel the map underneath must not zoom.
    return blocksInput(x, y);
}
