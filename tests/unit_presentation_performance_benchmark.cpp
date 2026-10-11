#include "config/unit_aware_view.h"
#include "config/unit_table_presentation.h"
#include "unit_test_check.h"

#include <QAbstractTableModel>
#include <QApplication>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTableView>
#include <QWidget>

#include <array>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string_view>

// Ordinary CTest regression with machine-independent structural checks.
// Timings are informational, not pass/fail thresholds. It measures the real Qt
// view invalidation and viewport painting paths using synthetic canonical data,
// without constructing an EPANET network.
class SyntheticNetworkTable final : public QAbstractTableModel
{
public:
    explicit SyntheticNetworkTable(UnitPresentationContext &context)
        : context_(context) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : this->network_rows_;
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(this->quantities_.size());
    }

    [[nodiscard]] double canonicalValue(int row, int column) const
    {
        // Stable numerical values model a large read-only hydraulic dataset.
        // This accessor is the authoritative value; formatting never changes it.
        return 0.1234567890123 + 0.017 * static_cast<double>(row + 1)
            + 0.31 * static_cast<double>(column + 1);
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= this->network_rows_
            || index.column() < 0 || index.column() >= columnCount())
            return {};
        const double canonical = canonicalValue(index.row(), index.column());
        if (role == Qt::UserRole)
            return canonical;
        if (role != Qt::DisplayRole)
            return {};
        ++this->display_reads_;
        const std::optional<double> value = UnitTablePresentation::toDisplayForColumn(
            this->context_, {this->quantities_[static_cast<std::size_t>(index.column())]},
            canonical);
        CHECK(value.has_value());
        return UnitTablePresentation::formatNumber(
            this->context_, *value, 4, QString(),
            this->quantities_[static_cast<std::size_t>(index.column())]);
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole)
            return {};
        if (orientation == Qt::Vertical)
            return section + 1;
        if (section < 0 || section >= columnCount())
            return {};
        const QString labels[] = {QStringLiteral("Head"), QStringLiteral("Flow"),
                                  QStringLiteral("Distance"), QStringLiteral("Volume")};
        return UnitTablePresentation::formattedTitle(labels[section], this->context_,
            {this->quantities_[static_cast<std::size_t>(section)]});
    }

    void presentationChanged()
    {
        ++this->presentation_invalidations_;
        emit headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
        emit dataChanged(index(0, 0), index(this->network_rows_ - 1, columnCount() - 1),
                         {Qt::DisplayRole});
    }

    [[nodiscard]] qint64 displayReads() const { return this->display_reads_; }
    [[nodiscard]] int presentationInvalidations() const {
        return this->presentation_invalidations_;
    }
    void clearDisplayReads() const { this->display_reads_ = 0; }

private:
    UnitPresentationContext &context_;
    const int network_rows_ = 100000;
    const std::array<std::string_view, 4> quantities_{
        "pressure_head", "volumetric_flow_rate", "length", "volume"};
    mutable qint64 display_reads_ = 0;
    int presentation_invalidations_ = 0;
};

static double milliseconds(const QElapsedTimer &timer)
{
    return static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    UnitPresentationContext context;
    const QJsonObject metric{
        {QStringLiteral("pressure_head"), QStringLiteral("m")},
        {QStringLiteral("volumetric_flow_rate"), QStringLiteral("m3/h")},
        {QStringLiteral("length"), QStringLiteral("m")},
        {QStringLiteral("volume"), QStringLiteral("m3")}};
    const QJsonObject customary{
        {QStringLiteral("pressure_head"), QStringLiteral("ft")},
        {QStringLiteral("volumetric_flow_rate"), QStringLiteral("US gal/min")},
        {QStringLiteral("length"), QStringLiteral("ft")},
        {QStringLiteral("volume"), QStringLiteral("ft3")}};
    context.initialize(metric);

    SyntheticNetworkTable table(context);
    QTableView visible_table;
    visible_table.setModel(&table);
    visible_table.setSortingEnabled(false);
    visible_table.verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    visible_table.verticalHeader()->setDefaultSectionSize(22);
    visible_table.resize(1024, 680);
    int visible_refreshes = 0;
    UnitAwareView table_binding(&visible_table, context, [&]() {
        ++visible_refreshes;
        table.presentationChanged();
    });
    visible_table.show();
    application.processEvents();
    CHECK(visible_refreshes == 1);
    CHECK(table.presentationInvalidations() == 1);

    const double before_head = table.canonicalValue(12345, 0);
    const double before_flow = table.canonicalValue(98765, 1);
    const qint64 total_cells = static_cast<qint64>(table.rowCount()) * table.columnCount();

    // A realistic viewport repaint should request only the cells that can
    // actually be seen, not a formatted copy of the entire 100k-row dataset.
    table.clearDisplayReads();
    QElapsedTimer timer;
    timer.start();
    CHECK(context.update(customary));
    application.processEvents();
    visible_table.viewport()->repaint();
    const double viewport_ms = milliseconds(timer);
    const qint64 viewport_reads = table.displayReads();
    CHECK(viewport_reads > 0);
    CHECK(viewport_reads < total_cells / 20); // Structural check, not timing.
    CHECK(table.canonicalValue(12345, 0) == before_head);
    CHECK(table.canonicalValue(98765, 1) == before_flow);

    // Many hidden presentation regions are subscribed but must do no work
    // until they are shown; a burst of profile changes coalesces into one
    // refresh for each such region.
    QWidget hidden_page;
    constexpr int hidden_regions = 250;
    int hidden_refreshes = 0;
    for (int i = 0; i < hidden_regions; ++i)
    {
        QWidget *child = new QWidget(&hidden_page);
        new UnitAwareView(child, context, [&hidden_refreshes]() { ++hidden_refreshes; });
        child->show();
    }
    CHECK(hidden_refreshes == 0);
    constexpr int switches = 500;
    const int previously_visible = visible_refreshes;
    timer.restart();
    for (int i = 0; i < switches; ++i)
        CHECK(context.update(i % 2 == 0 ? metric : customary));
    const double notifications_ms = milliseconds(timer);
    CHECK(hidden_refreshes == 0);
    const int hidden_before_show = hidden_refreshes;
    CHECK(visible_refreshes == previously_visible + switches);
    CHECK(table.presentationInvalidations() == visible_refreshes);
    CHECK(table.canonicalValue(12345, 0) == before_head);
    CHECK(table.canonicalValue(98765, 1) == before_flow);

    timer.restart();
    hidden_page.show();
    application.processEvents();
    const double catchup_ms = milliseconds(timer);
    CHECK(hidden_refreshes == hidden_regions);
    CHECK(!context.update(customary)); // Final iteration selected customary.
    CHECK(hidden_refreshes == hidden_regions);
    hidden_page.hide();

    // Repainting after the burst must remain viewport-limited.
    table.clearDisplayReads();
    timer.restart();
    visible_table.viewport()->repaint();
    const double repaint_ms = milliseconds(timer);
    const qint64 repaint_reads = table.displayReads();
    CHECK(repaint_reads > 0 && repaint_reads < total_cells / 20);

    QJsonObject report;
    report.insert(QStringLiteral("kind"), QStringLiteral("synthetic_unit_presentation_benchmark"));
    report.insert(QStringLiteral("rows"), table.rowCount());
    report.insert(QStringLiteral("columns"), table.columnCount());
    report.insert(QStringLiteral("total_cells"), static_cast<double>(total_cells));
    report.insert(QStringLiteral("viewport_refresh_ms"), viewport_ms);
    report.insert(QStringLiteral("viewport_display_reads"), static_cast<double>(viewport_reads));
    report.insert(QStringLiteral("hidden_regions"), hidden_regions);
    report.insert(QStringLiteral("profile_switches"), switches);
    report.insert(QStringLiteral("switch_notifications_ms"), notifications_ms);
    report.insert(QStringLiteral("hidden_refreshes_before_show"), hidden_before_show);
    report.insert(QStringLiteral("hidden_refreshes_after_show"), hidden_refreshes);
    report.insert(QStringLiteral("hidden_catchup_ms"), catchup_ms);
    report.insert(QStringLiteral("post_burst_viewport_repaint_ms"), repaint_ms);
    report.insert(QStringLiteral("post_burst_display_reads"), static_cast<double>(repaint_reads));
    report.insert(QStringLiteral("canonical_writes"), 0);
    const QByteArray json = QJsonDocument(report).toJson(QJsonDocument::Compact);
    std::fprintf(stdout, "%s\n", json.constData());
    return 0;
}
