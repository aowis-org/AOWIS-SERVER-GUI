#ifndef MAP_BGFX_EDITOR_ICONS_H
#define MAP_BGFX_EDITOR_ICONS_H

#include "map/bgfx/map_bgfx_editor_overlay.h"

#include <QHash>
#include <QImage>
#include <QString>

// Icon provider of the bgfx map editor overlay: the editor's full-colour
// entity pixmaps (MapEntityPixmapRenderer::pixmapPathForEntity()) at the
// marker width, with the selected / error / stale glows composited exactly
// like MapEntityPixmapRenderer::paint() does on the fly (28 tinted copies at
// 80 % opacity under the icon; selected + diagnostic: a blue outer glow and
// the diagnostic glow inside). Each (path, width, highlight) is composited
// once at the device pixel ratio into the overlay atlas and then drawn as
// one quad per entity.
class MapBgfxEditorIcons
{
public:
    MapBgfxEditorOverlay::Icon icon(MapBgfxHudTextCache *cache, InfrastructureEntity entity,
                                    const QString &pixmap_path, int width,
                                    MapBgfxEditorOverlay::Highlight highlight);

    // Glow reach of MapEntityPixmapRenderer (largest offset, logical px).
    static constexpr int GlowPaddingPx = 9;

private:
    const QImage &source(const QString &path);
    // The atlas lookup / composition behind icon().
    MapBgfxEditorOverlay::Icon typeIcon(MapBgfxHudTextCache *cache, InfrastructureEntity entity,
                                        const QString &pixmap_path, int width,
                                        MapBgfxEditorOverlay::Highlight highlight);

    QHash<QString, QImage> sources;
    // Per-frame fast path: the overlay asks once per visible entity (tens of
    // thousands with demand points), so the entity type's icon is memoized
    // by an integer key (entity, width, highlight) for the current atlas
    // generation; the string-keyed atlas lookup runs once per key only.
    QHash<quint64, MapBgfxEditorOverlay::Icon> type_icons;
    const MapBgfxHudTextCache *type_icons_cache = nullptr;
    quint64 type_icons_generation = 0;
};

#endif // MAP_BGFX_EDITOR_ICONS_H
