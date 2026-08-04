#include "entity_map_legend_dock.h"

#include <cmath>

#include <QColor>
#include <QLinearGradient>
#include <QLocale>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QSizePolicy>

namespace
{
QString legendGroupTitle(const QString &scope, const QString &metric, const QString &unit)
{
    if (unit.isEmpty())
        return QStringLiteral("%1 · %2").arg(scope, metric);

    return QStringLiteral("%1 · %2 [%3]").arg(scope, metric, unit);
}
}

class MapSymbologyRampWidget final : public QWidget
{
public:
    explicit MapSymbologyRampWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        this->setMinimumHeight(54);
        this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override
    {
        return QSize(220, 54);
    }

    void setRange(double minimum, double maximum, const QString &unit)
    {
        this->minimum = minimum;
        this->maximum = maximum;
        this->unit = unit;

        const QString minimum_text = this->formatValue(this->minimum);
        const QString maximum_text = this->formatValue(this->maximum);
        const QString unit_suffix = this->unit.isEmpty() ? QString() : QStringLiteral(" %1").arg(this->unit);
        this->setToolTip(QStringLiteral("Minimum: %1%3\nMaximum: %2%3").arg(minimum_text, maximum_text, unit_suffix));
        this->update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF available_rect = QRectF(this->rect()).adjusted(7.0, 4.0, -7.0, -4.0);
        const QRectF ramp_rect(available_rect.left(), available_rect.top(), available_rect.width(), 16.0);
        const QColor border_color = this->palette().color(QPalette::Mid);
        const QColor text_color = this->palette().color(QPalette::Text);
        const bool finite = std::isfinite(this->minimum) && std::isfinite(this->maximum);
        const bool uniform = finite && qFuzzyCompare(this->minimum + 1.0, this->maximum + 1.0);

        painter.setPen(QPen(border_color, 1.0));

        if (!finite)
        {
            painter.setBrush(this->palette().brush(QPalette::AlternateBase));
            painter.drawRoundedRect(ramp_rect, 4.0, 4.0);
            painter.setPen(text_color);
            painter.drawText(available_rect, Qt::AlignCenter, QStringLiteral("No finite values"));
            return;
        }

        if (uniform)
        {
            painter.setBrush(QColor(QStringLiteral("#21918c")));
        }
        else
        {
            QLinearGradient gradient(ramp_rect.topLeft(), ramp_rect.topRight());
            gradient.setColorAt(0.00, QColor(QStringLiteral("#440154")));
            gradient.setColorAt(0.25, QColor(QStringLiteral("#3b528b")));
            gradient.setColorAt(0.50, QColor(QStringLiteral("#21918c")));
            gradient.setColorAt(0.75, QColor(QStringLiteral("#5ec962")));
            gradient.setColorAt(1.00, QColor(QStringLiteral("#fde725")));
            painter.setBrush(gradient);
        }

        painter.drawRoundedRect(ramp_rect, 4.0, 4.0);

        const qreal tick_top = ramp_rect.bottom() + 2.0;
        const qreal tick_bottom = tick_top + 4.0;
        painter.drawLine(QPointF(ramp_rect.left(), tick_top), QPointF(ramp_rect.left(), tick_bottom));
        painter.drawLine(QPointF(ramp_rect.center().x(), tick_top), QPointF(ramp_rect.center().x(), tick_bottom));
        painter.drawLine(QPointF(ramp_rect.right(), tick_top), QPointF(ramp_rect.right(), tick_bottom));

        painter.setPen(text_color);
        const QRectF label_rect(available_rect.left(), tick_bottom + 1.0, available_rect.width(), available_rect.bottom() - tick_bottom - 1.0);

        if (uniform)
        {
            const QString value_text = this->formatValue(this->minimum);
            painter.drawText(label_rect, Qt::AlignHCenter | Qt::AlignTop, QStringLiteral("%1  ·  uniform").arg(value_text));
            return;
        }

        const QString minimum_text = this->formatValue(this->minimum);
        const QString midpoint_text = this->formatValue(this->minimum + ((this->maximum - this->minimum) * 0.5));
        const QString maximum_text = this->formatValue(this->maximum);

        painter.drawText(label_rect, Qt::AlignLeft | Qt::AlignTop, minimum_text);
        painter.drawText(label_rect, Qt::AlignHCenter | Qt::AlignTop, midpoint_text);
        painter.drawText(label_rect, Qt::AlignRight | Qt::AlignTop, maximum_text);
    }

private:
    double minimum = 0.0;
    double maximum = 0.0;
    QString unit;

    QString formatValue(double value) const
    {
        const double absolute_value = std::abs(value);
        int precision = 4;

        if (absolute_value >= 1000.0)
            precision = 5;
        else if (absolute_value >= 100.0)
            precision = 4;
        else if (absolute_value >= 10.0)
            precision = 4;
        else if (absolute_value >= 1.0)
            precision = 4;
        else if (absolute_value >= 0.01)
            precision = 3;

        return QLocale().toString(value, 'g', precision);
    }
};

EntityMapLegendDock::EntityMapLegendDock(HydraulicData *hydraulic_data, QWidget *parent)
    : QDockWidget("Map Symbology Legend", parent),
      hydraulic_data(hydraulic_data)
{
    this->setMinimumWidth(Sizes::SidebarRightWidth);
    this->resize(Sizes::SidebarRightWidth, this->height());
    this->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    this->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    this->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    QWidget *content = new QWidget(this);
    this->layout = new QVBoxLayout(content);
    this->layout->setContentsMargins(5, 5, 5, 5);
    this->layout->setSpacing(5);
    this->setWidget(content);

    this->addGroupNode();
    this->addGroupLink();
    this->addGroupHeatmap();

    this->group_node->setCollapsed(true);
    this->group_link->setCollapsed(true);
    this->group_heat->setCollapsed(true);

    this->layout->addStretch();
}

void EntityMapLegendDock::showMapLegendNode(VisualNode visual_node)
{
    this->visual_node = visual_node;
    this->setVisibility();

    if (visual_node == VisualNode::None)
    {
        this->group_node->setTitle(QStringLiteral("Node Legend"));
        this->group_node->setCollapsed(true);
        return;
    }

    this->updateNodeLegend();
    this->group_node->setCollapsed(false);
}

void EntityMapLegendDock::showMapLegendLink(VisualLink visual_link)
{
    this->visual_link = visual_link;
    this->setVisibility();

    if (visual_link == VisualLink::None)
    {
        this->group_link->setTitle(QStringLiteral("Link Legend"));
        this->group_link->setCollapsed(true);
        return;
    }

    this->updateLinkLegend();
    this->group_link->setCollapsed(false);
}

void EntityMapLegendDock::showMapLegendHeatmap(VisualHeatmap visual_heatmap)
{
    this->visual_heatmap = visual_heatmap;
    this->setVisibility();

    if (visual_heatmap == VisualHeatmap::None)
    {
        this->group_heat->setTitle(QStringLiteral("Heatmap Overlay"));
        this->group_heat->setCollapsed(true);
        return;
    }

    this->updateHeatmapLegend();
    this->group_heat->setCollapsed(false);
}

void EntityMapLegendDock::setMapMonitorActive(bool active)
{
    this->map_monitor_active = active;
    this->setVisibility();
}

void EntityMapLegendDock::setVisibility()
{
    const bool has_visible_legend = this->visual_link != VisualLink::None ||
                                    this->visual_node != VisualNode::None ||
                                    this->visual_heatmap != VisualHeatmap::None;
    this->setVisible(this->map_monitor_active && has_visible_legend);
}

void EntityMapLegendDock::addGroupNode()
{
    this->group_node = new GroupBoxCollapsible(QStringLiteral("Node Legend"), this);
    QVBoxLayout *group_layout = new QVBoxLayout(this->group_node);
    group_layout->setContentsMargins(6, 5, 6, 6);
    group_layout->setSpacing(0);

    this->legend_node = new MapSymbologyRampWidget(this->group_node);
    group_layout->addWidget(this->legend_node);
    this->layout->addWidget(this->group_node);
}

void EntityMapLegendDock::addGroupLink()
{
    this->group_link = new GroupBoxCollapsible(QStringLiteral("Link Legend"), this);
    QVBoxLayout *group_layout = new QVBoxLayout(this->group_link);
    group_layout->setContentsMargins(6, 5, 6, 6);
    group_layout->setSpacing(0);

    this->legend_link = new MapSymbologyRampWidget(this->group_link);
    group_layout->addWidget(this->legend_link);
    this->layout->addWidget(this->group_link);
}

void EntityMapLegendDock::addGroupHeatmap()
{
    this->group_heat = new GroupBoxCollapsible(QStringLiteral("Heatmap Overlay"), this);
    QVBoxLayout *group_layout = new QVBoxLayout(this->group_heat);
    group_layout->setContentsMargins(6, 5, 6, 6);
    group_layout->setSpacing(0);

    this->legend_heat = new MapSymbologyRampWidget(this->group_heat);
    group_layout->addWidget(this->legend_heat);
    this->layout->addWidget(this->group_heat);
}

void EntityMapLegendDock::updateNodeLegend()
{
    QString metric;
    QString unit;
    double minimum = 0.0;
    double maximum = 0.0;

    switch (this->visual_node)
    {
    case VisualNode::Elevation:
        metric = QStringLiteral("Elevation");
        unit = QStringLiteral("m");
        minimum = this->hydraulic_data->nodeElevationMMinimum();
        maximum = this->hydraulic_data->nodeElevationMMaximum();
        break;
    case VisualNode::BaseDemand:
        metric = QStringLiteral("Base Demand");
        unit = QStringLiteral("m³/h");
        minimum = this->hydraulic_data->nodeBaseDemandM3PerHMinimum();
        maximum = this->hydraulic_data->nodeBaseDemandM3PerHMaximum();
        break;
    case VisualNode::TotalDemand:
        metric = QStringLiteral("Total Demand");
        unit = QStringLiteral("m³/h");
        minimum = this->hydraulic_data->nodeTotalDemandM3PerHMinimum();
        maximum = this->hydraulic_data->nodeTotalDemandM3PerHMaximum();
        break;
    case VisualNode::DemandDeficit:
        metric = QStringLiteral("Demand Deficit");
        unit = QStringLiteral("m³/h");
        minimum = this->hydraulic_data->nodeDemandDeficitM3PerHMinimum();
        maximum = this->hydraulic_data->nodeDemandDeficitM3PerHMaximum();
        break;
    case VisualNode::EmitterFlow:
        metric = QStringLiteral("Emitter Flow");
        unit = QStringLiteral("m³/h");
        minimum = this->hydraulic_data->nodeEmitterFlowM3PerHMinimum();
        maximum = this->hydraulic_data->nodeEmitterFlowM3PerHMaximum();
        break;
    case VisualNode::Leakage:
        metric = QStringLiteral("Leakage");
        unit = QStringLiteral("m³/h");
        minimum = this->hydraulic_data->nodeLeakageM3PerHMinimum();
        maximum = this->hydraulic_data->nodeLeakageM3PerHMaximum();
        break;
    case VisualNode::Head:
        metric = QStringLiteral("Head");
        unit = QStringLiteral("m");
        minimum = this->hydraulic_data->nodeHeadMMinimum();
        maximum = this->hydraulic_data->nodeHeadMMaximum();
        break;
    case VisualNode::Pressure:
        metric = QStringLiteral("Pressure Head");
        unit = QStringLiteral("m");
        minimum = this->hydraulic_data->nodePressureMMinimum();
        maximum = this->hydraulic_data->nodePressureMMaximum();
        break;
    case VisualNode::Chlorine:
        metric = QStringLiteral("Chlorine");
        unit = QStringLiteral("mg/L");
        minimum = this->hydraulic_data->nodeChlorineMgPerLMinimum();
        maximum = this->hydraulic_data->nodeChlorineMgPerLMaximum();
        break;
    case VisualNode::RiverWater:
        metric = QStringLiteral("River Water");
        unit = QStringLiteral("%");
        minimum = this->hydraulic_data->nodeRiverWaterPercentMinimum();
        maximum = this->hydraulic_data->nodeRiverWaterPercentMaximum();
        break;
    case VisualNode::LakeWater:
        metric = QStringLiteral("Lake Water");
        unit = QStringLiteral("%");
        minimum = this->hydraulic_data->nodeLakeWaterPercentMinimum();
        maximum = this->hydraulic_data->nodeLakeWaterPercentMaximum();
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
    double minimum = 0.0;
    double maximum = 0.0;

    switch (this->visual_link)
    {
    case VisualLink::Diameter:
        metric = QStringLiteral("Diameter");
        unit = QStringLiteral("mm");
        minimum = this->hydraulic_data->linkDiameterMmMinimum();
        maximum = this->hydraulic_data->linkDiameterMmMaximum();
        break;
    case VisualLink::Length:
        metric = QStringLiteral("Length");
        unit = QStringLiteral("m");
        minimum = this->hydraulic_data->linkLengthMMinimum();
        maximum = this->hydraulic_data->linkLengthMMaximum();
        break;
    case VisualLink::Roughness:
        metric = QStringLiteral("Hazen-Williams C");
        minimum = this->hydraulic_data->linkRoughnessHwMinimum();
        maximum = this->hydraulic_data->linkRoughnessHwMaximum();
        break;
    case VisualLink::FlowRate:
        metric = QStringLiteral("Flow Rate");
        unit = QStringLiteral("m³/h");
        minimum = this->hydraulic_data->linkFlowRateM3PerHMinimum();
        maximum = this->hydraulic_data->linkFlowRateM3PerHMaximum();
        break;
    case VisualLink::Velocity:
        metric = QStringLiteral("Velocity");
        unit = QStringLiteral("m/s");
        minimum = this->hydraulic_data->linkVelocityMPerSMinimum();
        maximum = this->hydraulic_data->linkVelocityMPerSMaximum();
        break;
    case VisualLink::HeadLoss:
        metric = QStringLiteral("Head Loss");
        unit = QStringLiteral("m");
        minimum = this->hydraulic_data->linkHeadLossMMinimum();
        maximum = this->hydraulic_data->linkHeadLossMMaximum();
        break;
    case VisualLink::Leakage:
        metric = QStringLiteral("Leakage");
        unit = QStringLiteral("m³/h");
        minimum = this->hydraulic_data->linkLeakageM3PerHMinimum();
        maximum = this->hydraulic_data->linkLeakageM3PerHMaximum();
        break;
    case VisualLink::Chlorine:
        metric = QStringLiteral("Chlorine");
        unit = QStringLiteral("mg/L");
        minimum = this->hydraulic_data->linkChlorineMgPerLMinimum();
        maximum = this->hydraulic_data->linkChlorineMgPerLMaximum();
        break;
    case VisualLink::RiverWater:
        metric = QStringLiteral("River Water");
        unit = QStringLiteral("%");
        minimum = this->hydraulic_data->linkRiverWaterPercentMinimum();
        maximum = this->hydraulic_data->linkRiverWaterPercentMaximum();
        break;
    case VisualLink::LakeWater:
        metric = QStringLiteral("Lake Water");
        unit = QStringLiteral("%");
        minimum = this->hydraulic_data->linkLakeWaterPercentMinimum();
        maximum = this->hydraulic_data->linkLakeWaterPercentMaximum();
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
    double minimum = 0.0;
    double maximum = 0.0;

    switch (this->visual_heatmap)
    {
    case VisualHeatmap::Elevation:
        metric = QStringLiteral("Elevation");
        unit = QStringLiteral("m");
        minimum = this->hydraulic_data->heatmapElevationMMinimum();
        maximum = this->hydraulic_data->heatmapElevationMMaximum();
        break;
    case VisualHeatmap::BaseDemand:
        metric = QStringLiteral("Base Demand");
        unit = QStringLiteral("m³/h");
        minimum = this->hydraulic_data->nodeBaseDemandM3PerHMinimum();
        maximum = this->hydraulic_data->nodeBaseDemandM3PerHMaximum();
        break;
    case VisualHeatmap::TotalDemand:
        metric = QStringLiteral("Total Demand");
        unit = QStringLiteral("m³/h");
        minimum = this->hydraulic_data->heatmapTotalDemandM3PerHMinimum();
        maximum = this->hydraulic_data->heatmapTotalDemandM3PerHMaximum();
        break;
    case VisualHeatmap::DemandDeficit:
        metric = QStringLiteral("Demand Deficit");
        unit = QStringLiteral("m³/h");
        minimum = this->hydraulic_data->heatmapDemandDeficitM3PerHMinimum();
        maximum = this->hydraulic_data->heatmapDemandDeficitM3PerHMaximum();
        break;
    case VisualHeatmap::EmitterFlow:
        metric = QStringLiteral("Emitter Flow");
        unit = QStringLiteral("m³/h");
        minimum = this->hydraulic_data->nodeEmitterFlowM3PerHMinimum();
        maximum = this->hydraulic_data->nodeEmitterFlowM3PerHMaximum();
        break;
    case VisualHeatmap::Leakage:
        metric = QStringLiteral("Leakage");
        unit = QStringLiteral("m³/h");
        minimum = this->hydraulic_data->heatmapLeakageM3PerHMinimum();
        maximum = this->hydraulic_data->heatmapLeakageM3PerHMaximum();
        break;
    case VisualHeatmap::Head:
        metric = QStringLiteral("Head");
        unit = QStringLiteral("m");
        minimum = this->hydraulic_data->heatmapHeadMMinimum();
        maximum = this->hydraulic_data->heatmapHeadMMaximum();
        break;
    case VisualHeatmap::Pressure:
        metric = QStringLiteral("Pressure Head");
        unit = QStringLiteral("m");
        minimum = this->hydraulic_data->heatmapPressureMMinimum();
        maximum = this->hydraulic_data->heatmapPressureMMaximum();
        break;
    case VisualHeatmap::Chlorine:
        metric = QStringLiteral("Chlorine");
        unit = QStringLiteral("mg/L");
        minimum = this->hydraulic_data->heatmapChlorineMgPerLMinimum();
        maximum = this->hydraulic_data->heatmapChlorineMgPerLMaximum();
        break;
    case VisualHeatmap::RiverWater:
        metric = QStringLiteral("River Water");
        unit = QStringLiteral("%");
        minimum = this->hydraulic_data->heatmapRiverWaterPercentMinimum();
        maximum = this->hydraulic_data->heatmapRiverWaterPercentMaximum();
        break;
    case VisualHeatmap::LakeWater:
        metric = QStringLiteral("Lake Water");
        unit = QStringLiteral("%");
        minimum = this->hydraulic_data->heatmapLakeWaterPercentMinimum();
        maximum = this->hydraulic_data->heatmapLakeWaterPercentMaximum();
        break;
    case VisualHeatmap::None:
        return;
    }

    this->group_heat->setTitle(legendGroupTitle(QStringLiteral("Heatmap"), metric, unit));
    this->legend_heat->setRange(minimum, maximum, unit);
}
