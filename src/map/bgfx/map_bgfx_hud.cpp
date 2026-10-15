#include "map/bgfx/map_bgfx_hud.h"

#include <QFontMetricsF>

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

// Normalized Web Mercator world -> WGS84 degrees (inverse of worldFromWgs84).
void wgs84FromWorld(double world_x, double world_y, double *longitude_deg, double *latitude_deg)
{
    *longitude_deg = world_x * 360.0 - 180.0;
    *latitude_deg = std::atan(std::sinh(MapBgfx2d::Pi * (1.0 - 2.0 * world_y)))
        / MapBgfx2d::DegreesToRadians;
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
    return result;
}

void MapBgfxHud::setFont(const QFont &font)
{
    this->font = font;
    this->bold_font = font;
    this->bold_font.setBold(true);
    this->scale_font = font;
    this->scale_font.setPixelSize(ScaleLabelPixelSize);
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

void MapBgfxHud::drawPanel(MapBgfxHudCanvas *canvas, const MapBgfxHudRect &rect,
                           const Palette &palette)
{
    canvas->fillRoundedRect(rect, PanelRadiusPx, palette.panel);
    canvas->strokeRoundedRect(rect, PanelRadiusPx, 1.0, palette.border);
    this->blocking_panels.append(rect);
}

void MapBgfxHud::drawButton(MapBgfxHudCanvas *canvas, MapBgfxHudTextCache *cache,
                            const MapBgfxHudRect &rect, const QString &label, Element element,
                            bool enabled, bool checked, const Palette &palette)
{
    QRgb fill = palette.button;
    QRgb text_color = enabled ? palette.button_text : palette.disabled_text;
    if (checked)
    {
        fill = palette.highlight;
        text_color = palette.highlighted_text;
    }
    else if (enabled && this->armed == element && this->hovered == element)
    {
        fill = darker(palette.button, 112);
    }
    else if (enabled && this->hovered == element)
    {
        fill = lighter(palette.button, 106);
    }
    canvas->fillRoundedRect(rect, ButtonRadiusPx, fill);
    canvas->strokeRoundedRect(rect, ButtonRadiusPx, 1.0, palette.border);
    if (!label.isEmpty())
    {
        const MapBgfxHudSprite run = cache->text(label, this->font);
        const QSizeF size = cache->measure(label, this->font);
        const double ratio = cache->devicePixelRatio();
        const double x = std::round((rect.x + 0.5 * (rect.width - size.width())) * ratio) / ratio;
        const double y = std::round((rect.y + 0.5 * (rect.height - size.height())) * ratio) / ratio;
        canvas->sprite(x, y, run, text_color);
    }
    this->hit_boxes.append({rect, element, enabled});
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
    const std::function<double(double)> snap = [ratio](double value)
    {
        return std::round(value * ratio) / ratio;
    };

    double cursor = x + DownloadMarginLeftPx;
    canvas->sprite(snap(cursor), snap(y + 0.5 * (height - title_size.height())),
                   cache->text(title, this->bold_font), palette.text);
    cursor += title_size.width() + DownloadSpacingPx;

    // Indeterminate busy bar (QProgressBar range 0..0): a chunk sweeping
    // back and forth over a dim track.
    const MapBgfxHudRect track{snap(cursor), snap(y + 0.5 * (height - BusyBarHeightPx)),
                               BusyBarWidthPx, BusyBarHeightPx};
    canvas->fillRoundedRect(track, 2.0, withAlpha(palette.border, 110));
    const double chunk_width = 0.4 * BusyBarWidthPx;
    const double phase = std::fmod(std::max(0.0, time_s) * 0.8, 2.0);
    const double travel = phase < 1.0 ? phase : 2.0 - phase;
    canvas->fillRoundedRect(MapBgfxHudRect{track.x + travel * (BusyBarWidthPx - chunk_width),
                                           track.y, chunk_width, BusyBarHeightPx},
                            2.0, palette.highlight);
    cursor += BusyBarWidthPx + DownloadSpacingPx;

    canvas->sprite(snap(cursor), snap(y + 0.5 * (height - label_size.height())),
                   cache->text(label, this->font), palette.text);
    cursor += label_width + DownloadSpacingPx;

    const double button_height = buttonHeight(cache);
    drawButton(canvas, cache,
               MapBgfxHudRect{snap(cursor), snap(y + 0.5 * (height - button_height)),
                              cancel_width, button_height},
               cancel_text, cancel, !cancel_pending, false, palette);
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
    const double ratio = cache->devicePixelRatio();
    canvas->sprite(std::round((panel.x + 0.5 * (ScaleWidthPx - label_size.width())) * ratio) / ratio,
                   std::round((panel.y + 2.0 + 0.5 * (18.0 - label_size.height())) * ratio) / ratio,
                   cache->text(scale.label, this->scale_font), palette.text);

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
                   label_2d, Element::ViewMode2d, true, this->two_d, palette);
        drawButton(canvas, cache,
                   MapBgfxHudRect{frame.x + FramePaddingPx + width_2d, frame.y + FramePaddingPx,
                                  width_3d, segment_height},
                   label_3d, Element::ViewMode3d, this->globe_available, !this->two_d, palette);
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
        drawButton(canvas, cache, button, QString(), Element::Fullscreen, true, false, palette);
        const QImage &icon = this->fullscreen_active ? this->fullscreen_exit_image
                                                     : this->fullscreen_image;
        if (!icon.isNull())
        {
            const double icon_extent = FullscreenButtonExtentPx - 2.0;
            const MapBgfxHudSprite sprite = cache->image(
                this->fullscreen_active ? QStringLiteral("fullscreen-exit")
                                        : QStringLiteral("fullscreen"),
                icon, QSizeF(icon_extent, icon_extent));
            canvas->sprite(std::round((button.x + 1.0) * ratio) / ratio,
                           std::round((button.y + 1.0) * ratio) / ratio, sprite, 0xffffffffu);
        }
    }
}

MapBgfxHud::Element MapBgfxHud::elementAt(double x, double y) const
{
    for (const HitBox &box : this->hit_boxes)
    {
        if (box.enabled && box.rect.contains(x, y))
            return box.element;
    }
    return Element::None;
}

bool MapBgfxHud::blocksInput(double x, double y) const
{
    for (const MapBgfxHudRect &panel : this->blocking_panels)
    {
        if (panel.contains(x, y))
            return true;
    }
    return false;
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
        case Element::None:
            break;
    }
    return Action::None;
}

bool MapBgfxHud::mousePress(double x, double y)
{
    this->armed = elementAt(x, y);
    this->press_consumed = this->armed != Element::None || blocksInput(x, y);
    this->hovered = this->armed;
    return this->press_consumed;
}

MapBgfxHud::Action MapBgfxHud::mouseRelease(double x, double y, bool *consumed)
{
    const bool was_consumed = this->press_consumed;
    const Element armed_element = this->armed;
    this->press_consumed = false;
    this->armed = Element::None;
    if (consumed != nullptr)
        *consumed = was_consumed;
    if (armed_element == Element::None || elementAt(x, y) != armed_element)
        return Action::None;
    // A clicked Cancel stays disabled until fresh activity numbers arrive.
    if (armed_element == Element::CancelMapTiles)
        this->tiles_cancel_pending = true;
    if (armed_element == Element::CancelTerrain)
        this->terrain_cancel_pending = true;
    return actionFor(armed_element);
}

bool MapBgfxHud::mouseMove(double x, double y, bool *over_button)
{
    this->hovered = elementAt(x, y);
    if (over_button != nullptr)
        *over_button = this->hovered != Element::None;
    return this->press_consumed;
}
