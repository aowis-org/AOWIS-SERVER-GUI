#include "entity_map_legend_dock.h"

#include <array>
#include <cmath>

#include <QColor>
#include <QFontMetricsF>
#include <QHideEvent>
#include <QLinearGradient>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPointer>
#include <QSizePolicy>
#include <QTimer>

namespace
{
struct RampStop
{
    double position = 0.0;
    QColor color;
};

const std::array<RampStop, 7> ramp_stops = {{
    {0.000000, QColor(QStringLiteral("#440154"))},
    {0.166667, QColor(QStringLiteral("#443983"))},
    {0.333333, QColor(QStringLiteral("#31688e"))},
    {0.500000, QColor(QStringLiteral("#21918c"))},
    {0.666667, QColor(QStringLiteral("#35b779"))},
    {0.833333, QColor(QStringLiteral("#90d743"))},
    {1.000000, QColor(QStringLiteral("#fde725"))}
}};

QString legendGroupTitle(const QString &scope, const QString &metric, const QString &unit)
{
    if (unit.isEmpty())
        return QStringLiteral("%1 · %2").arg(scope, metric);

    return QStringLiteral("%1 · %2 [%3]").arg(scope, metric, unit);
}

QColor interpolateColor(const QColor &left, const QColor &right, double ratio)
{
    const double limited_ratio = qBound(0.0, ratio, 1.0);
    const int red = qRound(left.red() + ((right.red() - left.red()) * limited_ratio));
    const int green = qRound(left.green() + ((right.green() - left.green()) * limited_ratio));
    const int blue = qRound(left.blue() + ((right.blue() - left.blue()) * limited_ratio));
    return QColor(red, green, blue);
}
}

class MapSymbologyHoverSwatch final : public QWidget
{
public:
    explicit MapSymbologyHoverSwatch(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFixedSize(96, 52);
        hide();
    }

    void setDisplay(const QColor &color, const QString &text)
    {
        this->color = color;
        this->text = text;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(palette().color(QPalette::Mid), 1.0));
        painter.setBrush(this->color);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4.0, 4.0);

        QFont value_font = painter.font();
        value_font.setBold(true);
        value_font.setPointSizeF(value_font.pointSizeF() + 1.0);
        painter.setFont(value_font);

        const QRect text_rect = rect().adjusted(4, 2, -4, -2);
        painter.setPen(QColor(0, 0, 0, 180));
        painter.drawText(text_rect.translated(1, 1), Qt::AlignCenter, this->text);
        painter.setPen(Qt::white);
        painter.drawText(text_rect, Qt::AlignCenter, this->text);
    }

private:
    QColor color;
    QString text;
};

class MapSymbologyRampWidget final : public QWidget
{
public:
    explicit MapSymbologyRampWidget(QWidget *hover_parent, QWidget *parent = nullptr)
        : QWidget(parent),
          hover_parent(hover_parent),
          hover_swatch(new MapSymbologyHoverSwatch(hover_parent))
    {
        setMouseTracking(true);
        setFixedHeight(54);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    ~MapSymbologyRampWidget() override
    {
        delete this->hover_swatch.data();
    }

    QSize sizeHint() const override
    {
        return QSize(220, 54);
    }

    void setRange(double minimum, double maximum, const QString &unit)
    {
        this->value_minimum = minimum;
        this->value_maximum = maximum;
        this->unit = unit;
        hideHoverSwatch();

        const QString minimum_text = formatValue(this->value_minimum);
        const QString maximum_text = formatValue(this->value_maximum);
        const QString unit_suffix = this->unit.isEmpty() ? QString() : QStringLiteral(" %1").arg(this->unit);
        setToolTip(QStringLiteral("Minimum: %1%3\nMaximum: %2%3").arg(minimum_text, maximum_text, unit_suffix));
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF ramp_rect = rampRect();
        const QColor border_color = palette().color(QPalette::Mid);
        const QColor text_color = palette().color(QPalette::Text);
        const bool finite = std::isfinite(this->value_minimum) && std::isfinite(this->value_maximum);
        const bool uniform = finite && qFuzzyCompare(this->value_minimum + 1.0, this->value_maximum + 1.0);

        painter.setPen(QPen(border_color, 1.0));

        if (!finite)
        {
            painter.setBrush(palette().brush(QPalette::AlternateBase));
            painter.drawRoundedRect(ramp_rect, 4.0, 4.0);
            painter.setPen(text_color);
            painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("No finite values"));
            return;
        }

        if (uniform)
        {
            painter.setBrush(rampColor(0.5));
        }
        else
        {
            QLinearGradient gradient(ramp_rect.topLeft(), ramp_rect.topRight());
            for (const RampStop &stop : ramp_stops)
                gradient.setColorAt(stop.position, stop.color);
            painter.setBrush(gradient);
        }

        painter.drawRoundedRect(ramp_rect, 4.0, 4.0);
        drawTicksAndLabels(painter, ramp_rect, text_color, uniform);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const QRectF ramp_rect = rampRect();
        const bool finite = std::isfinite(this->value_minimum) && std::isfinite(this->value_maximum);

        if (finite && ramp_rect.contains(event->position()))
        {
            const double fraction = qBound(0.0, (event->position().x() - ramp_rect.left()) / ramp_rect.width(), 1.0);
            const bool uniform = qFuzzyCompare(this->value_minimum + 1.0, this->value_maximum + 1.0);
            showHoverSwatch(fraction, uniform);
            setCursor(Qt::CrossCursor);
        }
        else
        {
            hideHoverSwatch();
            unsetCursor();
        }

        QWidget::mouseMoveEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        hideHoverSwatch();
        unsetCursor();
        QWidget::leaveEvent(event);
    }

    void hideEvent(QHideEvent *event) override
    {
        hideHoverSwatch();
        QWidget::hideEvent(event);
    }

private:
    double value_minimum = 0.0;
    double value_maximum = 0.0;
    QString unit;
    QWidget *hover_parent = nullptr;
    QPointer<MapSymbologyHoverSwatch> hover_swatch;

    QRectF rampRect() const
    {
        const QRectF available_rect = QRectF(rect()).adjusted(7.0, 4.0, -7.0, -4.0);
        return QRectF(available_rect.left(), available_rect.top(), available_rect.width(), 16.0);
    }

    QColor rampColor(double fraction) const
    {
        const double limited_fraction = qBound(0.0, fraction, 1.0);

        for (std::size_t index = 1; index < ramp_stops.size(); ++index)
        {
            const RampStop &left = ramp_stops[index - 1];
            const RampStop &right = ramp_stops[index];

            if (limited_fraction > right.position)
                continue;

            const double interval = right.position - left.position;
            const double ratio = interval > 0.0 ? (limited_fraction - left.position) / interval : 0.0;
            return interpolateColor(left.color, right.color, ratio);
        }

        return ramp_stops.back().color;
    }

    void drawTicksAndLabels(QPainter &painter, const QRectF &ramp_rect, const QColor &text_color, bool uniform) const
    {
        const qreal tick_top = ramp_rect.bottom() + 2.0;
        const qreal tick_bottom = tick_top + 4.0;

        painter.setPen(text_color);
        QFont label_font = painter.font();
        label_font.setPointSizeF(qMax(6.0, label_font.pointSizeF() - 3.0));
        painter.setFont(label_font);

        const int label_count = labelCountForWidth(label_font, ramp_rect.width());
        for (int index = 0; index < label_count; ++index)
        {
            const double fraction = static_cast<double>(index) / static_cast<double>(label_count - 1);
            const qreal x = ramp_rect.left() + (ramp_rect.width() * fraction);
            painter.drawLine(QPointF(x, tick_top), QPointF(x, tick_bottom));
        }

        const QRectF label_area(ramp_rect.left(), tick_bottom + 2.0, ramp_rect.width(), height() - tick_bottom - 4.0);

        if (uniform)
        {
            painter.drawText(label_area, Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("%1 · uniform").arg(formatValue(this->value_minimum)));
            return;
        }

        const QFontMetricsF font_metrics(label_font);
        for (int index = 0; index < label_count; ++index)
        {
            const double fraction = static_cast<double>(index) / static_cast<double>(label_count - 1);
            const double value = this->value_minimum + ((this->value_maximum - this->value_minimum) * fraction);
            const QString text = formatValue(value);
            const qreal text_width = font_metrics.horizontalAdvance(text) + 2.0;
            const qreal label_center_x = ramp_rect.left() + (ramp_rect.width() * fraction);
            qreal label_left = label_center_x - (text_width * 0.5);
            Qt::Alignment alignment = Qt::AlignHCenter | Qt::AlignTop;

            if (index == 0)
            {
                label_left = ramp_rect.left();
                alignment = Qt::AlignLeft | Qt::AlignTop;
            }
            else if (index + 1 == label_count)
            {
                label_left = ramp_rect.right() - text_width;
                alignment = Qt::AlignRight | Qt::AlignTop;
            }

            const QRectF label_rect(label_left, label_area.top(), text_width, label_area.height());
            painter.drawText(label_rect, alignment, text);
        }
    }

    int labelCountForWidth(const QFont &font, qreal ramp_width) const
    {
        if (!std::isfinite(this->value_minimum) || !std::isfinite(this->value_maximum)
            || qFuzzyCompare(this->value_minimum + 1.0, this->value_maximum + 1.0))
        {
            return 7;
        }

        const QFontMetricsF font_metrics(font);
        const qreal minimum_gap = qMax(6.0, font_metrics.horizontalAdvance(QStringLiteral("  ")));
        constexpr std::array<int, 3> label_counts = {{7, 5, 3}};

        for (const int label_count : label_counts)
        {
            qreal previous_right = -minimum_gap;
            bool fits = true;

            for (int index = 0; index < label_count; ++index)
            {
                const double fraction = static_cast<double>(index) / static_cast<double>(label_count - 1);
                const double value = this->value_minimum + ((this->value_maximum - this->value_minimum) * fraction);
                const qreal text_width = font_metrics.horizontalAdvance(formatValue(value)) + 2.0;
                const qreal label_center_x = ramp_width * fraction;
                qreal label_left = label_center_x - (text_width * 0.5);
                qreal label_right = label_center_x + (text_width * 0.5);

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

    void showHoverSwatch(double fraction, bool uniform)
    {
        if (!this->hover_parent || !this->hover_swatch)
            return;

        const QRectF ramp_rect = rampRect();
        const qreal local_x = ramp_rect.left() + (ramp_rect.width() * fraction);
        const QPoint anchor = mapTo(this->hover_parent, QPoint(qRound(local_x), qRound(ramp_rect.top())));

        int swatch_x = anchor.x() - (this->hover_swatch->width() / 2);
        int swatch_y = anchor.y() - this->hover_swatch->height() - 4;
        swatch_x = qBound(2, swatch_x, qMax(2, this->hover_parent->width() - this->hover_swatch->width() - 2));

        if (swatch_y < 2)
            swatch_y = anchor.y() + qRound(ramp_rect.height()) + 4;

        const double value = uniform ? this->value_minimum : this->value_minimum + ((this->value_maximum - this->value_minimum) * fraction);
        this->hover_swatch->setDisplay(rampColor(uniform ? 0.5 : fraction), formatValue(value));
        this->hover_swatch->move(swatch_x, swatch_y);
        this->hover_swatch->show();
        this->hover_swatch->raise();
    }

    void hideHoverSwatch()
    {
        if (this->hover_swatch)
            this->hover_swatch->hide();
    }

    QString formatValue(double value) const
    {
        if (!std::isfinite(value))
            return QStringLiteral("—");

        const QLocale locale;
        const double absolute_value = std::abs(value);
        if (absolute_value == 0.0)
            return locale.toString(0.0, 'f', 2);

        const int decimal_exponent = static_cast<int>(std::floor(std::log10(absolute_value)));
        if (decimal_exponent >= 3 || decimal_exponent <= -3)
            return locale.toString(value, 'e', 2);

        const int decimal_places = qMax(0, 2 - decimal_exponent);
        const double decimal_factor = std::pow(10.0, decimal_places);
        const double rounded_value = std::round(value * decimal_factor) / decimal_factor;
        if (std::abs(rounded_value) >= 1000.0)
            return locale.toString(value, 'e', 2);

        return locale.toString(value, 'f', decimal_places);
    }
};

EntityMapLegendDock::EntityMapLegendDock(HydraulicData *hydraulic_data, QWidget *parent)
    : QDockWidget("Map Symbology Legend", parent),
      hydraulic_data(hydraulic_data)
{
    setMinimumWidth(Sizes::SidebarRightWidth);
    resize(Sizes::SidebarRightWidth, height());
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    this->content = new QWidget(this);
    this->content->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    this->layout = new QVBoxLayout(this->content);
    this->layout->setContentsMargins(5, 5, 5, 5);
    this->layout->setSpacing(5);
    setWidget(this->content);

    addGroupNode();
    addGroupLink();
    addGroupHeatmap();

    this->group_node->setCollapsed(true);
    this->group_link->setCollapsed(true);
    this->group_heat->setCollapsed(true);

    const std::array<GroupBoxCollapsible *, 3> groups = {{this->group_node, this->group_link, this->group_heat}};
    for (GroupBoxCollapsible *group : groups)
    {
        connect(group, &GroupBoxCollapsible::signalCollapsed, this, [this](GroupBoxCollapsible *)
        {
            scheduleDockHeightUpdate();
        });
        connect(group, &GroupBoxCollapsible::signalExpanded, this, [this](GroupBoxCollapsible *)
        {
            scheduleDockHeightUpdate();
        });
    }

    connect(this, &QDockWidget::topLevelChanged, this, [this](bool)
    {
        scheduleDockHeightUpdate();
    });
    connect(this, &QDockWidget::dockLocationChanged, this, [this](Qt::DockWidgetArea)
    {
        scheduleDockHeightUpdate();
    });

    scheduleDockHeightUpdate();
}

int EntityMapLegendDock::dockHeightPreferred() const
{
    return this->dock_height_preferred;
}

void EntityMapLegendDock::showMapLegendNode(VisualNode visual_node)
{
    this->visual_node = visual_node;
    setVisibility();

    if (visual_node == VisualNode::None)
    {
        this->group_node->setTitle(QStringLiteral("Node Legend"));
        this->group_node->setCollapsed(true);
        scheduleDockHeightUpdate();
        return;
    }

    updateNodeLegend();
    this->group_node->setCollapsed(false);
    scheduleDockHeightUpdate();
}

void EntityMapLegendDock::showMapLegendLink(VisualLink visual_link)
{
    this->visual_link = visual_link;
    setVisibility();

    if (visual_link == VisualLink::None)
    {
        this->group_link->setTitle(QStringLiteral("Link Legend"));
        this->group_link->setCollapsed(true);
        scheduleDockHeightUpdate();
        return;
    }

    updateLinkLegend();
    this->group_link->setCollapsed(false);
    scheduleDockHeightUpdate();
}

void EntityMapLegendDock::showMapLegendHeatmap(VisualHeatmap visual_heatmap)
{
    this->visual_heatmap = visual_heatmap;
    setVisibility();

    if (visual_heatmap == VisualHeatmap::None)
    {
        this->group_heat->setTitle(QStringLiteral("Heatmap Overlay"));
        this->group_heat->setCollapsed(true);
        scheduleDockHeightUpdate();
        return;
    }

    updateHeatmapLegend();
    this->group_heat->setCollapsed(false);
    scheduleDockHeightUpdate();
}

void EntityMapLegendDock::setMapMonitorActive(bool active)
{
    this->map_monitor_active = active;
    setVisibility();
}

void EntityMapLegendDock::setVisibility()
{
    const bool has_visible_legend = this->visual_link != VisualLink::None ||
                                    this->visual_node != VisualNode::None ||
                                    this->visual_heatmap != VisualHeatmap::None;
    setVisible(this->map_monitor_active && has_visible_legend);

    if (isVisible())
        scheduleDockHeightUpdate();
}

void EntityMapLegendDock::addGroupNode()
{
    this->group_node = new GroupBoxCollapsible(QStringLiteral("Node Legend"), this);
    QVBoxLayout *group_layout = new QVBoxLayout(this->group_node);
    group_layout->setContentsMargins(6, 5, 6, 6);
    group_layout->setSpacing(0);

    this->legend_node = new MapSymbologyRampWidget(this, this->group_node);
    group_layout->addWidget(this->legend_node);
    this->layout->addWidget(this->group_node);
}

void EntityMapLegendDock::addGroupLink()
{
    this->group_link = new GroupBoxCollapsible(QStringLiteral("Link Legend"), this);
    QVBoxLayout *group_layout = new QVBoxLayout(this->group_link);
    group_layout->setContentsMargins(6, 5, 6, 6);
    group_layout->setSpacing(0);

    this->legend_link = new MapSymbologyRampWidget(this, this->group_link);
    group_layout->addWidget(this->legend_link);
    this->layout->addWidget(this->group_link);
}

void EntityMapLegendDock::addGroupHeatmap()
{
    this->group_heat = new GroupBoxCollapsible(QStringLiteral("Heatmap Overlay"), this);
    QVBoxLayout *group_layout = new QVBoxLayout(this->group_heat);
    group_layout->setContentsMargins(6, 5, 6, 6);
    group_layout->setSpacing(0);

    this->legend_heat = new MapSymbologyRampWidget(this, this->group_heat);
    group_layout->addWidget(this->legend_heat);
    this->layout->addWidget(this->group_heat);
}

void EntityMapLegendDock::scheduleDockHeightUpdate()
{
    QTimer::singleShot(0, this, [this]()
    {
        updateDockHeight();
    });
}

void EntityMapLegendDock::updateDockHeight()
{
    if (!this->content || !this->layout)
        return;

    this->content->setMinimumHeight(0);
    this->content->setMaximumHeight(QWIDGETSIZE_MAX);

    this->layout->invalidate();
    this->layout->activate();
    const int content_height = qMax(this->layout->minimumSize().height(), this->layout->totalSizeHint().height());
    this->content->setFixedHeight(content_height);
    this->content->updateGeometry();

    if (QLayout *dock_layout = QDockWidget::layout())
    {
        dock_layout->invalidate();
        dock_layout->activate();
    }

    const int desired_height = qMax(content_height, QDockWidget::sizeHint().height());
    if (this->dock_height_preferred == desired_height)
        return;

    this->dock_height_preferred = desired_height;
    updateGeometry();
    emit signalDockHeightPreferredChanged(this->dock_height_preferred);
}

void EntityMapLegendDock::updateNodeLegend()
{
    QString metric;
    QString unit;
    NetworkSymbologySettings settings;
    settings.visual_node = this->visual_node;
    const NetworkSymbologyRanges ranges = this->hydraulic_data->symbologyRanges(settings);
    double minimum = ranges.node_minimum;
    double maximum = ranges.node_maximum;

    switch (this->visual_node)
    {
    case VisualNode::Elevation:
        metric = QStringLiteral("Elevation");
        unit = QStringLiteral("m");
        break;
    case VisualNode::BaseDemand:
        metric = QStringLiteral("Base Demand");
        unit = QStringLiteral("m³/h");
        break;
    case VisualNode::TotalDemand:
        metric = QStringLiteral("Total Demand");
        unit = QStringLiteral("m³/h");
        break;
    case VisualNode::DemandDeficit:
        metric = QStringLiteral("Demand Deficit");
        unit = QStringLiteral("m³/h");
        break;
    case VisualNode::EmitterFlow:
        metric = QStringLiteral("Emitter Flow");
        unit = QStringLiteral("m³/h");
        break;
    case VisualNode::Leakage:
        metric = QStringLiteral("Leakage");
        unit = QStringLiteral("m³/h");
        break;
    case VisualNode::Head:
        metric = QStringLiteral("Head");
        unit = QStringLiteral("m");
        break;
    case VisualNode::Pressure:
        metric = QStringLiteral("Pressure Head");
        unit = QStringLiteral("m");
        break;
    case VisualNode::Chlorine:
        metric = QStringLiteral("Chlorine");
        unit = QStringLiteral("mg/L");
        break;
    case VisualNode::RiverWater:
        metric = QStringLiteral("River Water");
        unit = QStringLiteral("%");
        break;
    case VisualNode::LakeWater:
        metric = QStringLiteral("Lake Water");
        unit = QStringLiteral("%");
        break;
    case VisualNode::None:
        return;
    }

    this->group_node->setTitle(legendGroupTitle(QStringLiteral("Node"), metric, unit));
    this->legend_node->setRange(minimum, maximum, unit);
}

void EntityMapLegendDock::updateLinkLegend()
{
    QString metric;
    QString unit;
    NetworkSymbologySettings settings;
    settings.visual_link = this->visual_link;
    const NetworkSymbologyRanges ranges = this->hydraulic_data->symbologyRanges(settings);
    double minimum = ranges.link_minimum;
    double maximum = ranges.link_maximum;

    switch (this->visual_link)
    {
    case VisualLink::Diameter:
        metric = QStringLiteral("Diameter");
        unit = QStringLiteral("mm");
        break;
    case VisualLink::Length:
    {
        metric = QStringLiteral("Length");
        const double maximum_absolute = qMax(std::abs(minimum), std::abs(maximum));
        if (maximum_absolute >= 1000.0)
        {
            unit = QStringLiteral("km");
            minimum /= 1000.0;
            maximum /= 1000.0;
        }
        else
        {
            unit = QStringLiteral("m");
        }
        break;
    }
    case VisualLink::Roughness:
        metric = QStringLiteral("Hazen-Williams C");
        break;
    case VisualLink::FlowRate:
        metric = QStringLiteral("Flow Rate");
        unit = QStringLiteral("m³/h");
        break;
    case VisualLink::Velocity:
        metric = QStringLiteral("Velocity");
        unit = QStringLiteral("m/s");
        break;
    case VisualLink::HeadLoss:
        metric = QStringLiteral("Head Loss");
        unit = QStringLiteral("m");
        break;
    case VisualLink::Leakage:
        metric = QStringLiteral("Leakage");
        unit = QStringLiteral("m³/h");
        break;
    case VisualLink::Chlorine:
        metric = QStringLiteral("Chlorine");
        unit = QStringLiteral("mg/L");
        break;
    case VisualLink::RiverWater:
        metric = QStringLiteral("River Water");
        unit = QStringLiteral("%");
        break;
    case VisualLink::LakeWater:
        metric = QStringLiteral("Lake Water");
        unit = QStringLiteral("%");
        break;
    case VisualLink::None:
        return;
    }

    this->group_link->setTitle(legendGroupTitle(QStringLiteral("Link"), metric, unit));
    this->legend_link->setRange(minimum, maximum, unit);
}

void EntityMapLegendDock::updateHeatmapLegend()
{
    QString metric;
    QString unit;
    NetworkSymbologySettings settings;
    settings.visual_heatmap = this->visual_heatmap;
    const NetworkSymbologyRanges ranges = this->hydraulic_data->symbologyRanges(settings);
    double minimum = ranges.heatmap_minimum;
    double maximum = ranges.heatmap_maximum;

    switch (this->visual_heatmap)
    {
    case VisualHeatmap::Elevation:
        metric = QStringLiteral("Elevation");
        unit = QStringLiteral("m");
        break;
    case VisualHeatmap::BaseDemand:
        metric = QStringLiteral("Base Demand");
        unit = QStringLiteral("m³/h");
        break;
    case VisualHeatmap::TotalDemand:
        metric = QStringLiteral("Total Demand");
        unit = QStringLiteral("m³/h");
        break;
    case VisualHeatmap::DemandDeficit:
        metric = QStringLiteral("Demand Deficit");
        unit = QStringLiteral("m³/h");
        break;
    case VisualHeatmap::EmitterFlow:
        metric = QStringLiteral("Emitter Flow");
        unit = QStringLiteral("m³/h");
        break;
    case VisualHeatmap::Leakage:
        metric = QStringLiteral("Leakage");
        unit = QStringLiteral("m³/h");
        break;
    case VisualHeatmap::Head:
        metric = QStringLiteral("Head");
        unit = QStringLiteral("m");
        break;
    case VisualHeatmap::Pressure:
        metric = QStringLiteral("Pressure Head");
        unit = QStringLiteral("m");
        break;
    case VisualHeatmap::Chlorine:
        metric = QStringLiteral("Chlorine");
        unit = QStringLiteral("mg/L");
        break;
    case VisualHeatmap::RiverWater:
        metric = QStringLiteral("River Water");
        unit = QStringLiteral("%");
        break;
    case VisualHeatmap::LakeWater:
        metric = QStringLiteral("Lake Water");
        unit = QStringLiteral("%");
        break;
    case VisualHeatmap::None:
        return;
    }

    this->group_heat->setTitle(legendGroupTitle(QStringLiteral("Heatmap"), metric, unit));
    this->legend_heat->setRange(minimum, maximum, unit);
}
