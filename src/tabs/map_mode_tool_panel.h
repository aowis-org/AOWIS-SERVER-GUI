#pragma once

#include <QWidget>

class QScrollArea;
class QVBoxLayout;

// Owns the left-side controls for one Maps mode, independently of its map
// viewport or renderer. It may be moved to the shared workspace in Stage 3
// without transferring ownership of MapWidget or MapBgfxSurface.
class MapModeToolPanel final : public QWidget
{
public:
    enum class ScrollbarWidthPolicy
    {
        Fixed,
        IncludeVisibleScrollbar
    };

    explicit MapModeToolPanel(QWidget *controls, int sidebar_width,
                              ScrollbarWidthPolicy width_policy,
                              bool frameless_scroll_area, QWidget *parent = nullptr);

    // Mode selector is a sibling of the scrolling controls, so the tabs
    // always remain accessible regardless of scroll position.
    void setModeSelector(QWidget *selector);
    QWidget *controls() const;
    QWidget *modeSelector() const;
    QScrollArea *scrollArea() const;

    // Hides just this panel during map fullscreen; does not touch the map.
    void setMapFullscreen(bool fullscreen);

private:
    QWidget *controls_widget = nullptr;
    QWidget *mode_selector = nullptr;
    QScrollArea *scroll_area = nullptr;
    QVBoxLayout *panel_layout = nullptr;
};
