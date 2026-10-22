#include "tabs/map_mode_tool_panel.h"

#include <QFrame>
#include <QScrollArea>
#include <QScrollBar>
#include <QSizePolicy>
#include <QVBoxLayout>

MapModeToolPanel::MapModeToolPanel(QWidget *controls, int sidebar_width,
                                   ScrollbarWidthPolicy width_policy,
                                   bool frameless_scroll_area, QWidget *parent)
    : QWidget(parent),
      controls_widget(controls),
      scroll_area(new QScrollArea(this)),
      panel_layout(new QVBoxLayout(this))
{
    Q_ASSERT(this->controls_widget != nullptr);
    Q_ASSERT(sidebar_width > 0);
    setObjectName(QStringLiteral("mapModeToolPanel"));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    this->panel_layout->setContentsMargins(0, 0, 0, 0);
    this->panel_layout->setSpacing(0);

    this->scroll_area->setObjectName(QStringLiteral("mapModeToolsScrollArea"));
    this->scroll_area->setMinimumWidth(sidebar_width);
    this->scroll_area->setMaximumWidth(sidebar_width);
    this->scroll_area->setWidgetResizable(true);
    this->scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    this->scroll_area->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    if (frameless_scroll_area)
        this->scroll_area->setFrameShape(QFrame::NoFrame);

    // Preserve the monitor's original width accommodation when the vertical
    // scrollbar appears. Editor controls retain their existing fixed width.
    if (width_policy == ScrollbarWidthPolicy::IncludeVisibleScrollbar)
    {
        connect(this->scroll_area->verticalScrollBar(), &QScrollBar::rangeChanged,
                this->scroll_area, [this, sidebar_width]()
        {
            QScrollBar *bar = this->scroll_area->verticalScrollBar();
            const int width = sidebar_width +
                (bar->maximum() > bar->minimum() ? bar->sizeHint().width() : 0);
            this->scroll_area->setMinimumWidth(width);
            this->scroll_area->setMaximumWidth(width);
        });
    }

    this->scroll_area->setWidget(this->controls_widget);
    this->panel_layout->addWidget(this->scroll_area);
}

void MapModeToolPanel::setModeSelector(QWidget *selector)
{
    if (selector == nullptr || this->mode_selector != nullptr)
        return;
    this->mode_selector = selector;
    this->panel_layout->insertWidget(0, selector);
}

QWidget *MapModeToolPanel::controls() const
{
    return this->controls_widget;
}

QWidget *MapModeToolPanel::modeSelector() const
{
    return this->mode_selector;
}

QScrollArea *MapModeToolPanel::scrollArea() const
{
    return this->scroll_area;
}

void MapModeToolPanel::setMapFullscreen(bool fullscreen)
{
    setVisible(!fullscreen);
}
