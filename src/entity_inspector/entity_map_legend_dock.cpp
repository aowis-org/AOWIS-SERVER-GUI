#include "entity_map_legend_dock.h"

#include "../gui_configuration.h"
#include "../network_symbology_rendering.h"

#include <array>
#include <cmath>
#include <functional>

#include <QAction>
#include <QColor>
#include <QFontMetricsF>
#include <QHideEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLinearGradient>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTimer>
#include <QWidgetAction>

namespace
{
const std::array<NetworkSymbologyPalette, NetworkSymbologyPaletteCount> palette_choices = {{
    NetworkSymbologyPalette::Viridis,
    NetworkSymbologyPalette::Cividis,
    NetworkSymbologyPalette::Plasma,
    NetworkSymbologyPalette::Inferno,
    NetworkSymbologyPalette::Magma,
    NetworkSymbologyPalette::Batlow,
    NetworkSymbologyPalette::Turbo,
    NetworkSymbologyPalette::CoolWarm,
    NetworkSymbologyPalette::RedBlue
}};

QString legendGroupTitle(const QString &scope, const QString &metric, const QString &unit)
{
    if (unit.isEmpty())
        return QStringLiteral("%1 · %2").arg(scope, metric);

    return QStringLiteral("%1 · %2 [%3]").arg(scope, metric, unit);
}

QIcon palettePreviewIcon(NetworkSymbologyPalette palette)
{
    QPixmap pixmap(112, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    QLinearGradient gradient(0.0, 0.0, pixmap.width(), 0.0);
    const std::array<QColor, NetworkSymbologyRampColorCount> &colors =
        networkSymbologyPaletteColors(palette);
    for (int index = 0; index < int(colors.size()); ++index)
    {
        gradient.setColorAt(
            double(index) / double(colors.size() - 1), colors.at(index));
    }
    painter.fillRect(pixmap.rect(), gradient);
    painter.setPen(QPen(QColor(0, 0, 0, 120), 1.0));
    painter.drawRect(pixmap.rect().adjusted(0, 0, -1, -1));
    return QIcon(pixmap);
}

struct LegendDescriptor
{
    QString metric;
    QString unit;
};

LegendDescriptor nodeLegendDescriptor(VisualNode visual_node)
{
    switch (visual_node)
    {
    case VisualNode::Elevation:
        return {QStringLiteral("Elevation"), QStringLiteral("m")};
    case VisualNode::BaseDemand:
        return {QStringLiteral("Base Demand"), QStringLiteral("m³/h")};
    case VisualNode::TotalDemand:
        return {QStringLiteral("Total Demand"), QStringLiteral("m³/h")};
    case VisualNode::DemandDeficit:
        return {QStringLiteral("Demand Deficit"), QStringLiteral("m³/h")};
    case VisualNode::EmitterFlow:
        return {QStringLiteral("Emitter Flow"), QStringLiteral("m³/h")};
    case VisualNode::Leakage:
        return {QStringLiteral("Leakage"), QStringLiteral("m³/h")};
    case VisualNode::Head:
        return {QStringLiteral("Head"), QStringLiteral("m")};
    case VisualNode::Pressure:
        return {QStringLiteral("Pressure Head"), QStringLiteral("m")};
    case VisualNode::Chlorine:
        return {QStringLiteral("Cl₂"), QStringLiteral("mg/L")};
    case VisualNode::RiverWater:
        return {QStringLiteral("River Water"), QStringLiteral("%")};
    case VisualNode::LakeWater:
        return {QStringLiteral("Lake Water"), QStringLiteral("%")};
    case VisualNode::WaterAge:
        return {QStringLiteral("Water Age"), QStringLiteral("h")};
    case VisualNode::None:
        return {QStringLiteral("None"), QString()};
    }

    return {QStringLiteral("None"), QString()};
}

LegendDescriptor linkLegendDescriptor(VisualLink visual_link)
{
    switch (visual_link)
    {
    case VisualLink::Diameter:
        return {QStringLiteral("Diameter"), QStringLiteral("mm")};
    case VisualLink::Length:
        return {QStringLiteral("Length"), QStringLiteral("m")};
    case VisualLink::Roughness:
        return {QStringLiteral("Hazen-Williams C"), QString()};
    case VisualLink::FlowRate:
        return {QStringLiteral("Flow Rate"), QStringLiteral("m³/h")};
    case VisualLink::Velocity:
        return {QStringLiteral("Velocity"), QStringLiteral("m/s")};
    case VisualLink::HeadLoss:
        return {QStringLiteral("Head Loss"), QStringLiteral("m")};
    case VisualLink::Leakage:
        return {QStringLiteral("Leakage"), QStringLiteral("m³/h")};
    case VisualLink::Chlorine:
        return {QStringLiteral("Cl₂"), QStringLiteral("mg/L")};
    case VisualLink::RiverWater:
        return {QStringLiteral("River Water"), QStringLiteral("%")};
    case VisualLink::LakeWater:
        return {QStringLiteral("Lake Water"), QStringLiteral("%")};
    case VisualLink::WaterAge:
        return {QStringLiteral("Water Age"), QStringLiteral("h")};
    case VisualLink::None:
        return {QStringLiteral("None"), QString()};
    }

    return {QStringLiteral("None"), QString()};
}

LegendDescriptor heatmapLegendDescriptor(VisualHeatmap visual_heatmap)
{
    switch (visual_heatmap)
    {
    case VisualHeatmap::Elevation:
        return {QStringLiteral("Elevation"), QStringLiteral("m")};
    case VisualHeatmap::BaseDemand:
        return {QStringLiteral("Base Demand"), QStringLiteral("m³/h")};
    case VisualHeatmap::TotalDemand:
        return {QStringLiteral("Total Demand"), QStringLiteral("m³/h")};
    case VisualHeatmap::DemandDeficit:
        return {QStringLiteral("Demand Deficit"), QStringLiteral("m³/h")};
    case VisualHeatmap::EmitterFlow:
        return {QStringLiteral("Emitter Flow"), QStringLiteral("m³/h")};
    case VisualHeatmap::Leakage:
        return {QStringLiteral("Leakage"), QStringLiteral("m³/h")};
    case VisualHeatmap::Head:
        return {QStringLiteral("Head"), QStringLiteral("m")};
    case VisualHeatmap::Pressure:
        return {QStringLiteral("Pressure Head"), QStringLiteral("m")};
    case VisualHeatmap::Chlorine:
        return {QStringLiteral("Cl₂"), QStringLiteral("mg/L")};
    case VisualHeatmap::RiverWater:
        return {QStringLiteral("River Water"), QStringLiteral("%")};
    case VisualHeatmap::LakeWater:
        return {QStringLiteral("Lake Water"), QStringLiteral("%")};
    case VisualHeatmap::WaterAge:
        return {QStringLiteral("Water Age"), QStringLiteral("h")};
    case VisualHeatmap::None:
        return {QStringLiteral("None"), QString()};
    }

    return {QStringLiteral("None"), QString()};
}

QString hudComboText(const QString &scope, const LegendDescriptor &descriptor)
{
    return legendGroupTitle(scope, descriptor.metric, descriptor.unit);
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
    using PaletteChangedCallback = std::function<void(NetworkSymbologyPalette, bool)>;

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

    void setPaletteChangedCallback(const PaletteChangedCallback &callback)
    {
        this->palette_changed_callback = callback;
    }

    void setDefaultPaletteSelection(NetworkSymbologyPalette palette, bool flipped)
    {
        this->default_palette = palette;
        this->default_palette_flipped = flipped;
    }

    void setPaletteSelection(NetworkSymbologyPalette palette, bool flipped)
    {
        if (this->palette == palette && this->palette_flipped == flipped)
            return;

        this->palette = palette;
        this->palette_flipped = flipped;
        hideHoverSwatch();
        updateToolTip();
        update();
    }

    void setRange(double minimum, double maximum, const QString &unit)
    {
        this->value_minimum = minimum;
        this->value_maximum = maximum;
        this->unit = unit;
        hideHoverSwatch();
        updateToolTip();
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF ramp_rect = rampRect();
        const QColor border_color = QWidget::palette().color(QPalette::Mid);
        const QColor text_color = QWidget::palette().color(QPalette::Text);
        const bool finite = std::isfinite(this->value_minimum) && std::isfinite(this->value_maximum);
        const bool uniform = finite && qFuzzyCompare(this->value_minimum + 1.0, this->value_maximum + 1.0);

        painter.setPen(QPen(border_color, 1.0));

        if (!finite)
        {
            painter.setBrush(QWidget::palette().brush(QPalette::AlternateBase));
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
            for (int index = 0; index < NetworkSymbologyRampColorCount; ++index)
            {
                const double fraction = double(index) / double(NetworkSymbologyRampColorCount - 1);
                gradient.setColorAt(fraction, rampColor(fraction));
            }
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

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rampRect().contains(event->position()))
        {
            hideHoverSwatch();
            showPaletteMenu();
            event->accept();
            return;
        }

        QWidget::mousePressEvent(event);
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
    NetworkSymbologyPalette palette = NetworkSymbologyPalette::Viridis;
    bool palette_flipped = false;
    NetworkSymbologyPalette default_palette = NetworkSymbologyPalette::Viridis;
    bool default_palette_flipped = false;
    PaletteChangedCallback palette_changed_callback;

    QRectF rampRect() const
    {
        const QRectF available_rect = QRectF(rect()).adjusted(7.0, 4.0, -7.0, -4.0);
        return QRectF(available_rect.left(), available_rect.top(), available_rect.width(), 16.0);
    }

    QColor rampColor(double fraction) const
    {
        return networkSymbologyInterpolatedRampColor(
            fraction, this->palette, this->palette_flipped);
    }

    void showPaletteMenu()
    {
        QMenu *menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
#ifdef Q_OS_WASM
        menu->setStyleSheet(QStringLiteral(
            "QMenu::item { min-height: 34px; padding: 4px 10px; }"));
#endif

        const QFontMetrics menu_font_metrics(menu->font());
        int palette_label_width = 0;
        for (const NetworkSymbologyPalette palette_choice : palette_choices)
        {
            palette_label_width = qMax(
                palette_label_width,
                menu_font_metrics.horizontalAdvance(
                    networkSymbologyPaletteName(palette_choice)));
        }
        palette_label_width += 8;

        constexpr int preview_width = 124;
#ifdef Q_OS_WASM
        constexpr int preview_height = 36;
#else
        constexpr int preview_height = 26;
#endif

        menu->addSection(QStringLiteral("Sequential"));

        for (const NetworkSymbologyPalette palette_choice : palette_choices)
        {
            if (palette_choice == NetworkSymbologyPalette::CoolWarm)
                menu->addSection(QStringLiteral("Diverging"));
            QWidget *row = new QWidget(menu);
            QHBoxLayout *row_layout = new QHBoxLayout(row);
            row_layout->setContentsMargins(6, 3, 6, 3);
            row_layout->setSpacing(8);

            QLabel *palette_label = new QLabel(
                networkSymbologyPaletteName(palette_choice), row);
            palette_label->setFixedWidth(palette_label_width);
            palette_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

            QPushButton *palette_button = new QPushButton(row);
            palette_button->setIcon(palettePreviewIcon(palette_choice));
            palette_button->setIconSize(QSize(112, 16));
            palette_button->setCheckable(true);
            palette_button->setChecked(this->palette == palette_choice);
            palette_button->setFixedSize(preview_width, preview_height);
            palette_button->setToolTip(
                QStringLiteral("Use %1 colors")
                    .arg(networkSymbologyPaletteName(palette_choice)));

            QPushButton *flip_button = new QPushButton(QStringLiteral("Flip"), row);
            flip_button->setCheckable(true);
            flip_button->setChecked(
                this->palette == palette_choice && this->palette_flipped);
            flip_button->setFixedHeight(preview_height);
            flip_button->setToolTip(QStringLiteral("Flip color direction"));

            row_layout->addWidget(palette_label);
            row_layout->addStretch(1);
            row_layout->addWidget(palette_button);
            row_layout->addWidget(flip_button);

            QWidgetAction *row_action = new QWidgetAction(menu);
            row_action->setDefaultWidget(row);
            menu->addAction(row_action);

            connect(palette_button, &QPushButton::clicked, menu,
                    [this, palette_choice, menu]
            {
                selectPalette(palette_choice, false);
                menu->close();
            });
            connect(flip_button, &QPushButton::clicked, menu,
                    [this, palette_choice, menu]
            {
                const bool flipped = this->palette == palette_choice
                    ? !this->palette_flipped
                    : true;
                selectPalette(palette_choice, flipped);
                menu->close();
            });
        }

        menu->addSeparator();
        QAction *reset_action = menu->addAction(QStringLiteral("Reset"));
        reset_action->setToolTip(QStringLiteral("Reset colors to the default palette"));
        connect(reset_action, &QAction::triggered, menu, [this, menu]
        {
            selectPalette(this->default_palette, this->default_palette_flipped);
            menu->close();
        });

        const QPoint popup_position = mapToGlobal(
            QPoint(0, qRound(rampRect().bottom()) + 4));
        menu->popup(popup_position);
    }

    void selectPalette(NetworkSymbologyPalette palette, bool flipped)
    {
        setPaletteSelection(palette, flipped);
        if (this->palette_changed_callback)
            this->palette_changed_callback(palette, flipped);
    }

    void updateToolTip()
    {
        const QString minimum_text = formatValue(this->value_minimum);
        const QString maximum_text = formatValue(this->value_maximum);
        const QString unit_suffix = this->unit.isEmpty()
            ? QString()
            : QStringLiteral(" %1").arg(this->unit);
        const QString palette_text = this->palette_flipped
            ? QStringLiteral("%1 · flipped").arg(networkSymbologyPaletteName(this->palette))
            : networkSymbologyPaletteName(this->palette);
        setToolTip(
            QStringLiteral("Minimum: %1%3\nMaximum: %2%3\nColors: %4\nClick to choose colors")
                .arg(minimum_text, maximum_text, unit_suffix, palette_text));
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

EntityMapLegendHud::EntityMapLegendHud(HydraulicData *hydraulic_data, QWidget *parent)
    : QWidget(parent),
      hydraulic_data(hydraulic_data),
      layout(new QVBoxLayout(this)),
      combo_node(new QComboBox(this)),
      combo_link(new QComboBox(this)),
      combo_heatmap(new QComboBox(this)),
      legend_node(new MapSymbologyRampWidget(this, this)),
      legend_link(new MapSymbologyRampWidget(this, this)),
      legend_heatmap(new MapSymbologyRampWidget(this, this))
{
    setAutoFillBackground(true);
    QPalette hud_palette = palette();
    QColor background = hud_palette.color(QPalette::Window);
    background.setAlpha(220);
    hud_palette.setColor(QPalette::Window, background);
    setPalette(hud_palette);

    setFixedWidth(Sizes::SidebarRightWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Maximum);
    this->layout->setContentsMargins(7, 7, 7, 7);
    this->layout->setSpacing(4);

    const std::array<VisualNode, 13> node_visuals = {{
        VisualNode::None,
        VisualNode::Elevation,
        VisualNode::BaseDemand,
        VisualNode::TotalDemand,
        VisualNode::DemandDeficit,
        VisualNode::EmitterFlow,
        VisualNode::Leakage,
        VisualNode::Head,
        VisualNode::Pressure,
        VisualNode::WaterAge,
        VisualNode::Chlorine,
        VisualNode::RiverWater,
        VisualNode::LakeWater
    }};
    for (const VisualNode visual_node : node_visuals)
    {
        this->combo_node->addItem(
            hudComboText(QStringLiteral("Node"), nodeLegendDescriptor(visual_node)),
            static_cast<int>(visual_node));
    }

    const std::array<VisualLink, 12> link_visuals = {{
        VisualLink::None,
        VisualLink::Diameter,
        VisualLink::Length,
        VisualLink::Roughness,
        VisualLink::FlowRate,
        VisualLink::Velocity,
        VisualLink::HeadLoss,
        VisualLink::Leakage,
        VisualLink::WaterAge,
        VisualLink::Chlorine,
        VisualLink::RiverWater,
        VisualLink::LakeWater
    }};
    for (const VisualLink visual_link : link_visuals)
    {
        this->combo_link->addItem(
            hudComboText(QStringLiteral("Link"), linkLegendDescriptor(visual_link)),
            static_cast<int>(visual_link));
    }

    const std::array<VisualHeatmap, 13> heatmap_visuals = {{
        VisualHeatmap::None,
        VisualHeatmap::Elevation,
        VisualHeatmap::BaseDemand,
        VisualHeatmap::TotalDemand,
        VisualHeatmap::DemandDeficit,
        VisualHeatmap::EmitterFlow,
        VisualHeatmap::Leakage,
        VisualHeatmap::Head,
        VisualHeatmap::Pressure,
        VisualHeatmap::WaterAge,
        VisualHeatmap::Chlorine,
        VisualHeatmap::RiverWater,
        VisualHeatmap::LakeWater
    }};
    for (const VisualHeatmap visual_heatmap : heatmap_visuals)
    {
        this->combo_heatmap->addItem(
            hudComboText(QStringLiteral("Heatmap"), heatmapLegendDescriptor(visual_heatmap)),
            static_cast<int>(visual_heatmap));
    }

    this->layout->addWidget(this->combo_node);
    this->layout->addWidget(this->legend_node);
    this->layout->addWidget(this->combo_link);
    this->layout->addWidget(this->legend_link);
    this->layout->addWidget(this->combo_heatmap);
    this->layout->addWidget(this->legend_heatmap);

    this->legend_node->setDefaultPaletteSelection(
        NetworkSymbologyDefaultNodePalette, false);
    this->legend_link->setDefaultPaletteSelection(
        NetworkSymbologyDefaultLinkPalette, false);
    this->legend_heatmap->setDefaultPaletteSelection(
        NetworkSymbologyDefaultHeatmapPalette, false);

    this->legend_node->setPaletteChangedCallback(
        [this](NetworkSymbologyPalette palette, bool flipped)
    {
        this->node_palette = palette;
        this->node_palette_flipped = flipped;
        emit signalNodePaletteSelected(palette, flipped);
    });
    this->legend_link->setPaletteChangedCallback(
        [this](NetworkSymbologyPalette palette, bool flipped)
    {
        this->link_palette = palette;
        this->link_palette_flipped = flipped;
        emit signalLinkPaletteSelected(palette, flipped);
    });
    this->legend_heatmap->setPaletteChangedCallback(
        [this](NetworkSymbologyPalette palette, bool flipped)
    {
        this->heatmap_palette = palette;
        this->heatmap_palette_flipped = flipped;
        emit signalHeatmapPaletteSelected(palette, flipped);
    });

    connect(this->combo_node, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        if (index < 0)
            return;
        this->visual_node = static_cast<VisualNode>(this->combo_node->itemData(index).toInt());
        updateNodeLegend();
        emit signalNodeVisualSelected(this->visual_node);
    });
    connect(this->combo_link, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        if (index < 0)
            return;
        this->visual_link = static_cast<VisualLink>(this->combo_link->itemData(index).toInt());
        updateLinkLegend();
        emit signalLinkVisualSelected(this->visual_link);
    });
    connect(this->combo_heatmap, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        if (index < 0)
            return;
        this->visual_heatmap = static_cast<VisualHeatmap>(this->combo_heatmap->itemData(index).toInt());
        updateHeatmapLegend();
        emit signalHeatmapVisualSelected(this->visual_heatmap);
    });

    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this, [this]
    {
        updateNodeLegend();
        updateLinkLegend();
        updateHeatmapLegend();
    });
    connect(this->hydraulic_data, &HydraulicData::signalNodeChanged, this,
            [this](InfrastructureEntity, const QUuid &)
    {
        updateNodeLegend();
        updateHeatmapLegend();
    });
    connect(this->hydraulic_data, &HydraulicData::signalLinkChanged, this,
            [this](InfrastructureEntity, const QUuid &)
    {
        updateLinkLegend();
    });
    connect(this->hydraulic_data, &HydraulicData::signalSimulationHeadlossFormulaChanged,
            this, [this]
    {
        if (this->visual_link == VisualLink::Roughness)
            updateLinkLegend();
    });
    connect(this->hydraulic_data, &HydraulicData::signalSimulationResultTimelineChanged,
            this, [this](bool)
    {
        updateNodeLegend();
        updateLinkLegend();
        updateHeatmapLegend();
    });
    connect(this->hydraulic_data, &HydraulicData::signalWaterQualitySimulationResultTimelineChanged,
            this, [this](bool)
    {
        updateNodeLegend();
        updateLinkLegend();
        updateHeatmapLegend();
    });
    connect(this->hydraulic_data, &HydraulicData::signalCurrentSimulationResultChanged,
            this, [this](int)
    {
        updateNodeLegend();
        updateLinkLegend();
        updateHeatmapLegend();
    });

    setNodeVisual(VisualNode::None);
    setLinkVisual(VisualLink::None);
    setHeatmapVisual(VisualHeatmap::None);
    updateVisibility();
}

void EntityMapLegendHud::setMapMonitorActive(bool active)
{
    this->map_monitor_active = active;
    updateVisibility();
}

void EntityMapLegendHud::setNodeVisual(VisualNode visual_node)
{
    this->visual_node = visual_node;
    const int index = this->combo_node->findData(static_cast<int>(visual_node));
    if (index >= 0)
    {
        const QSignalBlocker blocker(this->combo_node);
        this->combo_node->setCurrentIndex(index);
    }
    updateNodeLegend();
}

void EntityMapLegendHud::setLinkVisual(VisualLink visual_link)
{
    this->visual_link = visual_link;
    const int index = this->combo_link->findData(static_cast<int>(visual_link));
    if (index >= 0)
    {
        const QSignalBlocker blocker(this->combo_link);
        this->combo_link->setCurrentIndex(index);
    }
    updateLinkLegend();
}

void EntityMapLegendHud::setHeatmapVisual(VisualHeatmap visual_heatmap)
{
    this->visual_heatmap = visual_heatmap;
    const int index = this->combo_heatmap->findData(static_cast<int>(visual_heatmap));
    if (index >= 0)
    {
        const QSignalBlocker blocker(this->combo_heatmap);
        this->combo_heatmap->setCurrentIndex(index);
    }
    updateHeatmapLegend();
}

void EntityMapLegendHud::setNodePalette(NetworkSymbologyPalette palette, bool flipped)
{
    this->node_palette = palette;
    this->node_palette_flipped = flipped;
    this->legend_node->setPaletteSelection(palette, flipped);
}

void EntityMapLegendHud::setLinkPalette(NetworkSymbologyPalette palette, bool flipped)
{
    this->link_palette = palette;
    this->link_palette_flipped = flipped;
    this->legend_link->setPaletteSelection(palette, flipped);
}

void EntityMapLegendHud::setHeatmapPalette(NetworkSymbologyPalette palette, bool flipped)
{
    this->heatmap_palette = palette;
    this->heatmap_palette_flipped = flipped;
    this->legend_heatmap->setPaletteSelection(palette, flipped);
}

void EntityMapLegendHud::updateNodeLegend()
{
    this->legend_node->setPaletteSelection(
        this->node_palette, this->node_palette_flipped);
    const LegendDescriptor descriptor = nodeLegendDescriptor(this->visual_node);
    const int index = this->combo_node->findData(static_cast<int>(this->visual_node));
    if (index >= 0)
        this->combo_node->setItemText(index, hudComboText(QStringLiteral("Node"), descriptor));

    if (this->visual_node != VisualNode::None)
    {
        NetworkSymbologySettings settings;
        settings.visual_node = this->visual_node;
        const NetworkSymbologyRanges ranges = this->hydraulic_data->symbologyRanges(settings);
        this->legend_node->setRange(ranges.node_minimum, ranges.node_maximum, descriptor.unit);
    }

    updateVisibility();
}

void EntityMapLegendHud::updateLinkLegend()
{
    this->legend_link->setPaletteSelection(
        this->link_palette, this->link_palette_flipped);
    LegendDescriptor descriptor = linkLegendDescriptor(this->visual_link);
    NetworkSymbologyRanges ranges;

    if (this->visual_link != VisualLink::None)
    {
        NetworkSymbologySettings settings;
        settings.visual_link = this->visual_link;
        ranges = this->hydraulic_data->symbologyRanges(settings);

        if (this->visual_link == VisualLink::Length)
        {
            const double maximum_absolute = qMax(
                std::abs(ranges.link_minimum), std::abs(ranges.link_maximum));
            if (maximum_absolute >= 1000.0)
            {
                descriptor.unit = QStringLiteral("km");
                ranges.link_minimum /= 1000.0;
                ranges.link_maximum /= 1000.0;
            }
        }
        else if (this->visual_link == VisualLink::Roughness)
        {
            switch (this->hydraulic_data->networkHydraulic().options_hydraulic.headloss_formula)
            {
            case HydraulicHeadlossFormula::HazenWilliams:
                descriptor.metric = QStringLiteral("Hazen-Williams C");
                descriptor.unit.clear();
                break;
            case HydraulicHeadlossFormula::DarcyWeisbach:
                descriptor.metric = QStringLiteral("Absolute Roughness ε");
                descriptor.unit = QStringLiteral("mm");
                break;
            case HydraulicHeadlossFormula::ChezyManning:
                descriptor.metric = QStringLiteral("Manning Roughness n");
                descriptor.unit.clear();
                break;
            }
        }
    }

    const int index = this->combo_link->findData(static_cast<int>(this->visual_link));
    if (index >= 0)
        this->combo_link->setItemText(index, hudComboText(QStringLiteral("Link"), descriptor));

    if (this->visual_link != VisualLink::None)
        this->legend_link->setRange(ranges.link_minimum, ranges.link_maximum, descriptor.unit);

    updateVisibility();
}

void EntityMapLegendHud::updateHeatmapLegend()
{
    this->legend_heatmap->setPaletteSelection(
        this->heatmap_palette, this->heatmap_palette_flipped);
    const LegendDescriptor descriptor = heatmapLegendDescriptor(this->visual_heatmap);
    const int index = this->combo_heatmap->findData(static_cast<int>(this->visual_heatmap));
    if (index >= 0)
        this->combo_heatmap->setItemText(index, hudComboText(QStringLiteral("Heatmap"), descriptor));

    if (this->visual_heatmap != VisualHeatmap::None)
    {
        NetworkSymbologySettings settings;
        settings.visual_heatmap = this->visual_heatmap;
        const NetworkSymbologyRanges ranges = this->hydraulic_data->symbologyRanges(settings);
        this->legend_heatmap->setRange(
            ranges.heatmap_minimum, ranges.heatmap_maximum, descriptor.unit);
    }

    updateVisibility();
}

void EntityMapLegendHud::updateVisibility()
{
    this->legend_node->setVisible(this->visual_node != VisualNode::None);
    this->legend_link->setVisible(this->visual_link != VisualLink::None);
    this->legend_heatmap->setVisible(this->visual_heatmap != VisualHeatmap::None);
    setVisible(this->map_monitor_active);
    adjustSize();
    updateGeometry();
}

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

    this->legend_node->setDefaultPaletteSelection(
        NetworkSymbologyDefaultNodePalette, false);
    this->legend_link->setDefaultPaletteSelection(
        NetworkSymbologyDefaultLinkPalette, false);
    this->legend_heat->setDefaultPaletteSelection(
        NetworkSymbologyDefaultHeatmapPalette, false);

    const GuiSymbologyPaletteConfiguration &palette_configuration =
        guiConfiguration().symbology_palettes;
    setNodePalette(
        palette_configuration.node_palette, palette_configuration.node_palette_flipped);
    setLinkPalette(
        palette_configuration.link_palette, palette_configuration.link_palette_flipped);
    setHeatmapPalette(
        palette_configuration.heatmap_palette, palette_configuration.heatmap_palette_flipped);

    this->legend_node->setPaletteChangedCallback(
        [this](NetworkSymbologyPalette palette, bool flipped)
    {
        this->node_palette = palette;
        this->node_palette_flipped = flipped;
        emit signalNodePaletteSelected(palette, flipped);
    });
    this->legend_link->setPaletteChangedCallback(
        [this](NetworkSymbologyPalette palette, bool flipped)
    {
        this->link_palette = palette;
        this->link_palette_flipped = flipped;
        emit signalLinkPaletteSelected(palette, flipped);
    });
    this->legend_heat->setPaletteChangedCallback(
        [this](NetworkSymbologyPalette palette, bool flipped)
    {
        this->heatmap_palette = palette;
        this->heatmap_palette_flipped = flipped;
        emit signalHeatmapPaletteSelected(palette, flipped);
    });

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

    connect(this->hydraulic_data, &HydraulicData::signalNetworkLoaded, this, [this]
    {
        updateNodeLegend();
        updateLinkLegend();
        updateHeatmapLegend();
    });
    connect(this->hydraulic_data, &HydraulicData::signalNodeChanged, this,
        [this](InfrastructureEntity, const QUuid &)
    {
        updateNodeLegend();
        updateHeatmapLegend();
    });
    connect(this->hydraulic_data, &HydraulicData::signalLinkChanged, this,
        [this](InfrastructureEntity, const QUuid &)
    {
        updateLinkLegend();
    });
    connect(this->hydraulic_data, &HydraulicData::signalWaterQualitySimulationResultTimelineChanged,
        this, [this](bool)
    {
        if (this->visual_node == VisualNode::WaterAge)
            updateNodeLegend();
        if (this->visual_link == VisualLink::WaterAge)
            updateLinkLegend();
        if (this->visual_heatmap == VisualHeatmap::WaterAge)
            updateHeatmapLegend();
    });
    connect(this->hydraulic_data, &HydraulicData::signalCurrentSimulationResultChanged,
        this, [this](int)
    {
        if (this->visual_node == VisualNode::WaterAge)
            updateNodeLegend();
        if (this->visual_link == VisualLink::WaterAge)
            updateLinkLegend();
        if (this->visual_heatmap == VisualHeatmap::WaterAge)
            updateHeatmapLegend();
    });

    scheduleDockHeightUpdate();
}

int EntityMapLegendDock::dockHeightPreferred() const
{
    return this->dock_height_preferred;
}

void EntityMapLegendDock::configureAsHud()
{
    setAllowedAreas(Qt::NoDockWidgetArea);
    setFeatures(QDockWidget::NoDockWidgetFeatures);

    QWidget *title_bar = new QWidget(this);
    title_bar->setFixedHeight(0);
    setTitleBarWidget(title_bar);

    setMinimumWidth(300);
    setMaximumWidth(Sizes::SidebarRightWidth);
    resize(Sizes::SidebarRightWidth, height());

    if (this->content != nullptr)
    {
        this->content->setAutoFillBackground(true);
        QPalette hud_palette = this->content->palette();
        QColor background = hud_palette.color(QPalette::Window);
        background.setAlpha(220);
        hud_palette.setColor(QPalette::Window, background);
        this->content->setPalette(hud_palette);
    }

    scheduleDockHeightUpdate();
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

void EntityMapLegendDock::setNodePalette(NetworkSymbologyPalette palette, bool flipped)
{
    this->node_palette = palette;
    this->node_palette_flipped = flipped;
    this->legend_node->setPaletteSelection(palette, flipped);
}

void EntityMapLegendDock::setLinkPalette(NetworkSymbologyPalette palette, bool flipped)
{
    this->link_palette = palette;
    this->link_palette_flipped = flipped;
    this->legend_link->setPaletteSelection(palette, flipped);
}

void EntityMapLegendDock::setHeatmapPalette(NetworkSymbologyPalette palette, bool flipped)
{
    this->heatmap_palette = palette;
    this->heatmap_palette_flipped = flipped;
    this->legend_heat->setPaletteSelection(palette, flipped);
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
    this->legend_node->setPaletteSelection(
        this->node_palette, this->node_palette_flipped);
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
    case VisualNode::WaterAge:
        metric = QStringLiteral("Water Age");
        unit = QStringLiteral("h");
        break;
    case VisualNode::None:
        return;
    }

    this->group_node->setTitle(legendGroupTitle(QStringLiteral("Node"), metric, unit));
    this->legend_node->setRange(minimum, maximum, unit);
}

void EntityMapLegendDock::updateLinkLegend()
{
    this->legend_link->setPaletteSelection(
        this->link_palette, this->link_palette_flipped);
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
    case VisualLink::WaterAge:
        metric = QStringLiteral("Water Age");
        unit = QStringLiteral("h");
        break;
    case VisualLink::None:
        return;
    }

    this->group_link->setTitle(legendGroupTitle(QStringLiteral("Link"), metric, unit));
    this->legend_link->setRange(minimum, maximum, unit);
}

void EntityMapLegendDock::updateHeatmapLegend()
{
    this->legend_heat->setPaletteSelection(
        this->heatmap_palette, this->heatmap_palette_flipped);
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
    case VisualHeatmap::WaterAge:
        metric = QStringLiteral("Water Age");
        unit = QStringLiteral("h");
        break;
    case VisualHeatmap::None:
        return;
    }

    this->group_heat->setTitle(legendGroupTitle(QStringLiteral("Heatmap"), metric, unit));
    this->legend_heat->setRange(minimum, maximum, unit);
}
