#include "config/unit_aware_view.h"
#include "config/unit_numeric_spin_box.h"
#include "config/unit_table_presentation.h"
#include "entity_inspector/map_legend_unit_presentation.h"
#include "map/monitor/map_hud_length_units.h"
#include "simulation/simulation_inspector_unit_presentation.h"
#include "simulation/simulation_statistics_unit_presentation.h"
#include "unit_test_check.h"

#include <QAbstractTableModel>
#include <QApplication>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>

#include <cmath>
#include <cstring>
#include <optional>
#include <string_view>

// A cross-surface integration regression for the production unit presentation
// helpers, actual Qt controls, tab lifecycle and table viewport. It does NOT
// instantiate the actual hydraulic Model, 2D/3D renderers or EPANET solver;
// their end-to-end acceptance remains a separate live-application gate.
class UnitAcceptanceTable final : public QAbstractTableModel
{
public:
    explicit UnitAcceptanceTable(UnitPresentationContext &context, QObject *parent = nullptr)
        : QAbstractTableModel(parent), context_(context) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : 25000;
    }

    int columnCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : 2;
    }

    [[nodiscard]] double canonicalValue(int row, int column) const
    {
        return (column == 0 ? 12.345678912345 : 1.2345678912345)
            + static_cast<double>(row) * 0.001;
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()
            || index.column() < 0 || index.column() >= columnCount())
            return {};
        const double canonical = canonicalValue(index.row(), index.column());
        if (role == Qt::UserRole) return canonical;
        if (role != Qt::DisplayRole) return {};
        ++this->display_reads_;
        const std::string_view quantity = index.column() == 0
            ? "pressure_head" : "volumetric_flow_rate";
        const std::optional<double> displayed = UnitTablePresentation::toDisplay(
            this->context_, quantity, canonical);
        CHECK(displayed.has_value());
        return UnitTablePresentation::formatNumber(
            this->context_, *displayed, 4, QString(), quantity);
    }

    QVariant headerData(int column, Qt::Orientation orientation, int role) const override
    {
        if (role != Qt::DisplayRole) return {};
        if (orientation == Qt::Vertical) return column + 1;
        if (column < 0 || column >= columnCount()) return {};
        return UnitTablePresentation::formattedTitle(
            column == 0 ? QStringLiteral("Head") : QStringLiteral("Flow"),
            this->context_,
            {column == 0 ? std::string_view("pressure_head")
                         : std::string_view("volumetric_flow_rate")});
    }

    void invalidatePresentation()
    {
        ++this->invalidations_;
        emit headerDataChanged(Qt::Horizontal, 0, columnCount() - 1);
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                         {Qt::DisplayRole});
    }

    [[nodiscard]] int invalidations() const { return this->invalidations_; }
    [[nodiscard]] int displayReads() const { return this->display_reads_; }
    void clearDisplayReads() const { this->display_reads_ = 0; }

private:
    UnitPresentationContext &context_;
    mutable int display_reads_ = 0;
    int invalidations_ = 0;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    UnitPresentationContext context;
    const QJsonObject metric{
        {QStringLiteral("pressure_head"), QStringLiteral("m")},
        {QStringLiteral("hydraulic_head"), QStringLiteral("m")},
        {QStringLiteral("volumetric_flow_rate"), QStringLiteral("m3/h")},
        {QStringLiteral("length"), QStringLiteral("m")}};
    const QJsonObject customary{
        {QStringLiteral("pressure_head"), QStringLiteral("ft")},
        {QStringLiteral("hydraulic_head"), QStringLiteral("ft")},
        {QStringLiteral("volumetric_flow_rate"), QStringLiteral("US gal/min")},
        {QStringLiteral("length"), QStringLiteral("ft")}};
    context.initialize(metric);

    // Deliberately more precise than the editor's three-decimal display.
    double canonical_head = 123.456789123456;
    const double original_head = canonical_head;
    const double world_distance_m = 2034.567891234;
    const double simulation_flow_m3h = 1.2345678901234;
    int canonical_writes = 0;

    QTabWidget tabs;
    tabs.resize(1000, 640);
    QWidget inspector;
    QVBoxLayout inspector_layout(&inspector);
    UnitNumericSpinBox head_editor(
        [&]() -> std::optional<double> { return canonical_head; },
        [&](double next) { canonical_head = next; ++canonical_writes; return true; },
        "pressure_head", "m", 0.0, 5000.0, 0.1, 3, &inspector);
    inspector_layout.addWidget(&head_editor);
    int inspector_refreshes = 0;
    UnitAwareView inspector_binding(&inspector, context, [&] {
        ++inspector_refreshes;
        CHECK(head_editor.refreshUnits(context));
    });
    tabs.addTab(&inspector, QStringLiteral("Inspector"));

    QWidget table_page;
    QVBoxLayout table_layout(&table_page);
    UnitAcceptanceTable table_model(context);
    QTableView table_view;
    table_view.setModel(&table_model);
    table_view.setSortingEnabled(false);
    table_view.verticalHeader()->setDefaultSectionSize(22);
    table_layout.addWidget(&table_view);
    int table_refreshes = 0;
    UnitAwareView table_binding(&table_page, context, [&] {
        ++table_refreshes;
        table_model.invalidatePresentation();
    });
    tabs.addTab(&table_page, QStringLiteral("Entity table"));

    QWidget map_2d_page;
    QVBoxLayout map_2d_layout(&map_2d_page);
    QLabel map_2d_label;
    map_2d_layout.addWidget(&map_2d_label);
    int map_2d_refreshes = 0;
    UnitAwareView map_2d_binding(&map_2d_page, context, [&] {
        ++map_2d_refreshes;
        const MapLegendUnitPresentation::Range range =
            MapLegendUnitPresentation::range(context, QStringLiteral("Length"),
                                             0.0, world_distance_m);
        map_2d_label.setText(MapHudLengthUnits::networkOffset(world_distance_m, context)
                             + QStringLiteral(" / ") + range.unit);
    });
    tabs.addTab(&map_2d_page, QStringLiteral("2D label presentation"));

    QWidget map_3d_page;
    QVBoxLayout map_3d_layout(&map_3d_page);
    QLabel map_3d_label;
    map_3d_layout.addWidget(&map_3d_label);
    int map_3d_refreshes = 0;
    UnitAwareView map_3d_binding(&map_3d_page, context, [&] {
        ++map_3d_refreshes;
        map_3d_label.setText(MapHudLengthUnits::cameraDistance(world_distance_m, context));
    });
    tabs.addTab(&map_3d_page, QStringLiteral("3D HUD presentation"));

    QWidget simulation_page;
    QVBoxLayout simulation_layout(&simulation_page);
    QLabel simulation_label;
    simulation_layout.addWidget(&simulation_label);
    int simulation_refreshes = 0;
    UnitAwareView simulation_binding(&simulation_page, context, [&] {
        ++simulation_refreshes;
        simulation_label.setText(SimulationInspectorUnitPresentation::format(
            context, "volumetric_flow_rate", simulation_flow_m3h));
    });
    tabs.addTab(&simulation_page, QStringLiteral("Simulation presentation"));

    tabs.show();
    app.processEvents();
    CHECK(inspector_refreshes == 1);
    CHECK(table_refreshes == 0 && map_2d_refreshes == 0);
    CHECK(map_3d_refreshes == 0 && simulation_refreshes == 0);
    CHECK(head_editor.suffix().contains(QStringLiteral("m")));
    CHECK(canonical_writes == 0);

    // Only the active inspector changes; every other tab stays untouched.
    CHECK(context.update(customary));
    app.processEvents();
    CHECK(inspector_refreshes == 2);
    CHECK(table_refreshes == 0 && map_2d_refreshes == 0);
    CHECK(map_3d_refreshes == 0 && simulation_refreshes == 0);
    CHECK(head_editor.suffix().contains(QStringLiteral("ft")));
    CHECK(std::memcmp(&canonical_head, &original_head, sizeof(double)) == 0);
    CHECK(canonical_writes == 0);
    CHECK(head_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(canonical_writes == 0);

    // Hidden tab catches up once to the latest generation, without scanning
    // all canonical rows or altering the numerical sort role.
    CHECK(context.update(metric));
    CHECK(context.update(customary));
    CHECK(table_refreshes == 0);
    tabs.setCurrentWidget(&table_page);
    app.processEvents();
    CHECK(table_refreshes == 1 && table_model.invalidations() == 1);
    CHECK(table_model.headerData(0, Qt::Horizontal, Qt::DisplayRole)
          .toString().contains(QStringLiteral("ft")));
    CHECK(table_model.headerData(1, Qt::Horizontal, Qt::DisplayRole)
          .toString().contains(QStringLiteral("US gal/min")));
    const double table_canonical_before = table_model.canonicalValue(12000, 0);
    CHECK(table_model.data(table_model.index(12000, 0), Qt::UserRole).toDouble()
          == table_canonical_before);
    table_model.clearDisplayReads();
    CHECK(context.update(metric));
    app.processEvents();
    table_view.viewport()->repaint();
    CHECK(table_refreshes == 2);
    CHECK(table_model.displayReads() > 0);
    CHECK(table_model.displayReads() < table_model.rowCount() / 20);
    CHECK(table_model.data(table_model.index(12000, 0), Qt::UserRole).toDouble()
          == table_canonical_before);
    CHECK(canonical_writes == 0);

    // Actual labels/formatters shared with map presentation, not the map
    // renderer itself. Both hidden pages must defer their refresh work.
    CHECK(context.update(customary));
    CHECK(map_2d_refreshes == 0 && map_3d_refreshes == 0);
    tabs.setCurrentWidget(&map_2d_page);
    app.processEvents();
    CHECK(map_2d_refreshes == 1);
    CHECK(map_2d_label.text().contains(QStringLiteral("ft")));
    CHECK(map_2d_label.text().contains(QStringLiteral("mi")));
    tabs.setCurrentWidget(&map_3d_page);
    app.processEvents();
    CHECK(map_3d_refreshes == 1);
    CHECK(map_3d_label.text().contains(QStringLiteral("mi")));

    tabs.setCurrentWidget(&simulation_page);
    app.processEvents();
    CHECK(simulation_refreshes == 1);
    CHECK(simulation_label.text().contains(QStringLiteral("US gal/min")));
    CHECK(SimulationStatisticsUnits::formatWithUnit(context, false, simulation_flow_m3h)
          .contains(QStringLiteral("US gal/min")));
    CHECK(canonical_writes == 0);
    CHECK(std::memcmp(&canonical_head, &original_head, sizeof(double)) == 0);

    // Switching profiles repeatedly while the inspector is hidden must not
    // reconfigure its rounded editor or generate a Model write. One catch-up
    // refresh occurs when it becomes visible again.
    const int previous_inspector_refreshes = inspector_refreshes;
    for (int i = 0; i < 100; ++i)
        CHECK(context.update(i % 2 == 0 ? metric : customary));
    CHECK(inspector_refreshes == previous_inspector_refreshes);
    tabs.setCurrentWidget(&inspector);
    app.processEvents();
    CHECK(inspector_refreshes == previous_inspector_refreshes + 1);
    CHECK(canonical_writes == 0);
    CHECK(std::memcmp(&canonical_head, &original_head, sizeof(double)) == 0);

    // Only an explicit user action is allowed to quantize the canonical value.
    head_editor.stepUp();
    CHECK(canonical_writes == 1);
    CHECK(canonical_head != original_head);
    CHECK(world_distance_m == 2034.567891234);
    CHECK(simulation_flow_m3h == 1.2345678901234);
    return 0;
}
