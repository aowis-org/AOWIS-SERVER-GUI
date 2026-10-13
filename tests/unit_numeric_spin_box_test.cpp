#include "config/unit_numeric_spin_box.h"
#include "config/unit_energy_price_spin_box.h"
#include "config/unit_bulk_reaction_display.h"
#include "config/unit_aware_view.h"
#include <aowis/model/hydraulic/hydraulic_types.h>
#include <QApplication>
#include <QKeyEvent>
#include <memory>
#include <stdexcept>
#include <vector>
#include <functional>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #condition); } while (false)

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    double elevation = 123.456789123;
    const double original = elevation;
    int writes = 0;
    bool available = true;
    UnitPresentationContext context;
    context.initialize(QJsonObject{{QStringLiteral("elevation"), QStringLiteral("m")},
                                  {QStringLiteral("volumetric_flow_rate"), QStringLiteral("L/s")}});
    UnitNumericSpinBox editor(
        [&]() -> std::optional<double> { return available ? std::optional<double>(elevation) : std::nullopt; },
        [&](double value) { elevation = value; ++writes; return true; },
        "elevation", "m", -10000.0, 10000.0, 0.1, 3);
    int refreshes = 0;
    UnitAwareView view(&editor, context, [&] { ++refreshes; editor.refreshUnits(context); });
    editor.show();
    CHECK(editor.value() == 123.457 && elevation == original && writes == 0);
    editor.hide();
    const int before_hidden = refreshes;
    context.update(QJsonObject{{QStringLiteral("elevation"), QStringLiteral("ft")}});
    CHECK(refreshes == before_hidden && writes == 0);
    editor.show();
    CHECK(refreshes == before_hidden + 1 && elevation == original);
    CHECK(editor.suffix() == NumericText::suffix(QStringLiteral("ft")));
    CHECK(editor.commitEdit() == NumericPropertyBinding::Result::Unchanged && elevation == original);
    QLineEdit *text = editor.findChild<QLineEdit *>();
    CHECK(text != nullptr);
    text->selectAll();
    QKeyEvent input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QStringLiteral("10"));
    QApplication::sendEvent(text, &input);
    context.update(QJsonObject{{QStringLiteral("elevation"), QStringLiteral("m")}});
    CHECK(editor.displayUnit() == "ft");
    CHECK(editor.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(elevation - 3.048) < 1e-12 && writes == 1);
    CHECK(editor.displayUnit() == "m");
    available = false;
    CHECK(!editor.refreshUnits(context) && !editor.isEnabled() && writes == 1);

    // A unit requiring 17 edit decimals must not print 17 fractional zeros.
    // Keeping Qt's actual decimals setting allows edits at full resolution.
    double wall_rate_per_day = 108000.0;
    int wall_rate_writes = 0;
    UnitPresentationContext wall_rate_context;
    wall_rate_context.initialize(QJsonObject{
        {QStringLiteral("zero_order_wall_reaction_coefficient"), QStringLiteral("mg/m2/s")}});
    wall_rate_context.setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    UnitNumericSpinBox wall_rate_editor(
        [&] { return std::optional<double>(wall_rate_per_day); },
        [&](double candidate) { wall_rate_per_day = candidate; ++wall_rate_writes; return true; },
        "zero_order_wall_reaction_coefficient", "mg/m2/d",
        -1000000.0, 1000000.0, 0.001, 12);
    CHECK(wall_rate_editor.refreshUnits(wall_rate_context));
    CHECK(wall_rate_editor.decimals() == 17);
    CHECK(wall_rate_editor.cleanText() == QStringLiteral("1.25"));
    CHECK(wall_rate_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(wall_rate_per_day == 108000.0 && wall_rate_writes == 0);
    wall_rate_per_day = 0.0;
    CHECK(wall_rate_editor.refreshUnits(wall_rate_context));
    CHECK(wall_rate_editor.cleanText() == QStringLiteral("0"));
    CHECK(wall_rate_editor.decimals() == 17 && wall_rate_writes == 0);
    QLineEdit *wall_rate_text = wall_rate_editor.findChild<QLineEdit *>();
    CHECK(wall_rate_text != nullptr);
    wall_rate_text->selectAll();
    QKeyEvent wall_rate_input(QEvent::KeyPress, Qt::Key_unknown,
                              Qt::NoModifier, QStringLiteral("0.00000000012345678"));
    QApplication::sendEvent(wall_rate_text, &wall_rate_input);
    CHECK(wall_rate_editor.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(wall_rate_per_day - 0.00000000012345678 * 86400.0) < 1e-14);
    CHECK(wall_rate_writes == 1);

    // Decimal precision is specified in stored units. Displaying the same
    // 0.1-mm pipe-diameter resolution in m/ft must add fractional digits,
    // including when a fixed formatting policy asks for fewer digits.
    double precise_diameter_mm = 150.037;
    int precise_diameter_writes = 0;
    UnitPresentationContext precision_context;
    precision_context.initialize(QJsonObject{{QStringLiteral("link_diameter"), QStringLiteral("mm")},
                                             {QStringLiteral("length"), QStringLiteral("m")}});
    UnitNumericSpinBox precise_diameter(
        [&] { return std::optional<double>(precise_diameter_mm); },
        [&](double millimetres) {
            precise_diameter_mm = millimetres;
            ++precise_diameter_writes;
            return true;
        }, "link_diameter", "mm", 0.0, 5000.0, 10.0, 1);
    CHECK(precise_diameter.refreshUnits(precision_context));
    CHECK(precise_diameter.decimals() == 1);
    CHECK(precision_context.update(QJsonObject{{QStringLiteral("link_diameter"), QStringLiteral("m")},
                                               {QStringLiteral("length"), QStringLiteral("m")}}));
    CHECK(precise_diameter.refreshUnits(precision_context));
    CHECK(precise_diameter.decimals() == 4);
    CHECK(std::abs(precise_diameter.value() - 0.15) < 1e-12);
    CHECK(precise_diameter.singleStep() == 0.01);
    CHECK(precise_diameter_writes == 0 && precise_diameter_mm == 150.037);
    precise_diameter.stepUp();
    CHECK(precise_diameter.lastResult() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(precise_diameter_mm - 160.037) < 1e-10);
    CHECK(std::abs(precise_diameter.value() - 0.16) < 1e-12);
    precise_diameter.stepDown();
    CHECK(std::abs(precise_diameter_mm - 150.037) < 1e-10);
    for (int index = 0; index < 10; ++index) precise_diameter.stepUp();
    for (int index = 0; index < 10; ++index) precise_diameter.stepDown();
    CHECK(std::abs(precise_diameter_mm - 150.037) < 1e-9);
    const int before_fixed = precise_diameter_writes;
    CHECK(precision_context.setFormatPolicy(
        {NumericFormatPolicy::Notation::Fixed, 1, false, true},
        QStringLiteral("link_diameter")));
    CHECK(precise_diameter.refreshUnits(precision_context));
    CHECK(precise_diameter.decimals() == 4);
    CHECK(precise_diameter_writes == before_fixed);
    QLineEdit *precision_text = precise_diameter.findChild<QLineEdit *>();
    CHECK(precision_text != nullptr);
    // An edit equal to the rounded visible value must not silently discard
    // finer stored precision: 0.15 m is already represented as 0.1500 m.
    precision_text->selectAll();
    QKeyEvent same_value_input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier,
                               QStringLiteral("0.15"));
    QApplication::sendEvent(precision_text, &same_value_input);
    CHECK(precise_diameter.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(std::abs(precise_diameter_mm - 150.037) < 1e-10);
    CHECK(precise_diameter_writes == before_fixed);
    // A genuinely different typed value must commit exactly in the selected
    // display unit, after which the physical arrow step is preserved.
    precision_text->selectAll();
    QKeyEvent changed_value_input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier,
                                  QStringLiteral("0.17"));
    QApplication::sendEvent(precision_text, &changed_value_input);
    CHECK(precise_diameter.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(precise_diameter_mm - 170.0) < 1e-10);
    precise_diameter.stepUp();
    CHECK(std::abs(precise_diameter_mm - 180.0) < 1e-10);
    CHECK(precision_context.update(QJsonObject{{QStringLiteral("link_diameter"), QStringLiteral("ft")},
                                               {QStringLiteral("length"), QStringLiteral("m")}}));
    CHECK(precise_diameter.refreshUnits(precision_context));
    CHECK(precise_diameter.decimals() == 4);
    CHECK(precise_diameter_writes == before_fixed + 2);

    double precise_length_m = 123.456789012345;
    int precise_length_writes = 0;
    UnitNumericSpinBox precise_length(
        [&] { return std::optional<double>(precise_length_m); },
        [&](double metres) {
            precise_length_m = metres;
            ++precise_length_writes;
            return true;
        }, "length", "m", 0.0, 1000000.0, 1.0, 2);
    CHECK(precise_length.refreshUnits(precision_context));
    CHECK(precise_length.decimals() == 2);
    CHECK(precision_context.update(QJsonObject{{QStringLiteral("link_diameter"), QStringLiteral("ft")},
                                               {QStringLiteral("length"), QStringLiteral("km")}}));
    CHECK(precise_length.refreshUnits(precision_context));
    CHECK(precise_length.decimals() == 5);
    CHECK(precision_context.update(QJsonObject{{QStringLiteral("link_diameter"), QStringLiteral("ft")},
                                               {QStringLiteral("length"), QStringLiteral("mi")}}));
    CHECK(precise_length.refreshUnits(precision_context));
    CHECK(precise_length.decimals() == 6);
    CHECK(precise_length_m == 123.456789012345 && precise_length_writes == 0);
    precise_length.stepUp();
    CHECK(std::abs(precise_length_m - 124.456789012345) < 1e-10);

    // A pressure editor presents the same metre-head storage as psi without
    // rewriting on refresh; physical pressure depends on project gravity.
    double pressure_head_m = 10.0;
    int pressure_writes = 0;
    UnitPresentationContext pressure_context;
    pressure_context.initialize(QJsonObject{{QStringLiteral("pressure_head"), QStringLiteral("psi")}});
    CHECK(pressure_context.setSpecificGravity(1.2));
    UnitNumericSpinBox pressure_editor(
        [&] { return std::optional<double>(pressure_head_m); },
        [&](double canonical) { pressure_head_m = canonical; ++pressure_writes; return true; },
        "pressure_head", "m", 0.0, 100.0, 1.0, 2);
    CHECK(pressure_editor.refreshUnits(pressure_context));
    const double psi_per_m = 1.2 * 0.4333 / 0.3048;
    CHECK(std::abs(pressure_editor.value() - 10.0 * psi_per_m) < 0.01);
    CHECK(pressure_head_m == 10.0 && pressure_writes == 0);
    pressure_editor.stepUp();
    CHECK(std::abs(pressure_head_m - 11.0) < 1e-9 && pressure_writes == 1);

    double demand = 3.6;
    UnitNumericSpinBox flow([&] { return std::optional<double>(demand); },
        [&](double value) { demand = value; ++writes; return true; },
        "base_demand_flow_rate", "m3/h", -1e9, 1e9, 0.1, 6);
    context.update(QJsonObject{{QStringLiteral("volumetric_flow_rate"), QStringLiteral("L/s")}});
    CHECK(flow.refreshUnits(context));
    CHECK(flow.value() == 1.0 && demand == 3.6);
    text = flow.findChild<QLineEdit *>();
    CHECK(text != nullptr);
    text->selectAll();
    QKeyEvent flow_input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QStringLiteral("2"));
    QApplication::sendEvent(text, &flow_input);
    CHECK(flow.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(demand - 7.2) < 1e-12 && writes == 2);
    // An explicit canonical base-demand unit must override generic flow L/s.
    QJsonObject independent_base_demand = context.effectiveUnits();
    independent_base_demand.insert(QStringLiteral("base_demand_flow_rate"),
                                   QStringLiteral("m3/h"));
    CHECK(context.update(independent_base_demand));
    CHECK(flow.refreshUnits(context));
    CHECK(flow.suffix() == NumericText::suffix(QStringLiteral("m3/h")));
    CHECK(demand == 7.2 && writes == 2);
    independent_base_demand.remove(QStringLiteral("base_demand_flow_rate"));
    CHECK(context.update(independent_base_demand));
    CHECK(flow.refreshUnits(context));
    CHECK(flow.suffix() == NumericText::suffix(QStringLiteral("L/s")));
    CHECK(demand == 7.2 && writes == 2);

    // Demand Point rows use the same canonical-backed flow bindings as
    // Junction rows. Merely formatting a precise value (or switching units)
    // cannot replace its stored value with the rounded spin-box value.
    std::vector<double> demand_point_flows{2.345678901234, 6.543210987654};
    const double original_demand_point_flow = demand_point_flows[0];
    int demand_point_writes = 0;
    bool demand_rows_stale = false;
    UnitNumericSpinBox demand_point_flow_editor(
        [&]() -> std::optional<double> {
            if (demand_rows_stale || demand_point_flows.empty())
                return std::nullopt;
            return demand_point_flows[0];
        },
        [&](double value) {
            if (demand_rows_stale || demand_point_flows.empty())
                return false;
            demand_point_flows[0] = value;
            ++demand_point_writes;
            return true;
        }, "customer_demand_flow_rate", "m3/h", -1e9, 1e9, 0.1, 6);
    context.update(QJsonObject{
        {QStringLiteral("volumetric_flow_rate"), QStringLiteral("L/s")}});
    CHECK(demand_point_flow_editor.refreshUnits(context));
    CHECK(demand_point_flow_editor.suffix() == NumericText::suffix(QStringLiteral("L/s")));
    CHECK(std::abs(demand_point_flow_editor.value() -
                   original_demand_point_flow / 3.6) < 0.000001);
    CHECK(demand_point_flow_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    context.update(QJsonObject{
        {QStringLiteral("volumetric_flow_rate"), QStringLiteral("m3/h")}});
    CHECK(demand_point_flow_editor.refreshUnits(context));
    CHECK(demand_point_writes == 0);
    CHECK(demand_point_flows[0] == original_demand_point_flow);
    QJsonObject independent_customer_demand{
        {QStringLiteral("volumetric_flow_rate"), QStringLiteral("L/s")},
        {QStringLiteral("customer_demand_flow_rate"), QStringLiteral("m3/h")}};
    CHECK(context.update(independent_customer_demand));
    CHECK(demand_point_flow_editor.refreshUnits(context));
    CHECK(demand_point_flow_editor.suffix() == NumericText::suffix(QStringLiteral("m3/h")));
    CHECK(demand_point_writes == 0);
    independent_customer_demand.remove(QStringLiteral("customer_demand_flow_rate"));
    CHECK(context.update(independent_customer_demand));
    CHECK(demand_point_flow_editor.refreshUnits(context));
    CHECK(demand_point_flow_editor.suffix() == NumericText::suffix(QStringLiteral("L/s")));
    CHECK(demand_point_writes == 0);
    QLineEdit *demand_point_text = demand_point_flow_editor.findChild<QLineEdit *>();
    CHECK(demand_point_text != nullptr);
    demand_point_text->selectAll();
    QKeyEvent demand_point_input(QEvent::KeyPress, Qt::Key_unknown,
                                 Qt::NoModifier, QStringLiteral("1.25"));
    QApplication::sendEvent(demand_point_text, &demand_point_input);
    CHECK(demand_point_flow_editor.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(demand_point_flows[0] - 4.5) < 1e-12);
    CHECK(demand_point_writes == 1);

    // Removing a category while an edit is pending must not redirect that
    // edit to another category with the same former row index.
    UnitNumericSpinBox removed_demand_point_flow_editor(
        [&]() -> std::optional<double> {
            if (demand_rows_stale || demand_point_flows.size() < 2)
                return std::nullopt;
            return demand_point_flows[1];
        },
        [&](double value) {
            if (demand_rows_stale || demand_point_flows.size() < 2)
                return false;
            demand_point_flows[1] = value;
            ++demand_point_writes;
            return true;
        }, "customer_demand_flow_rate", "m3/h", -1e9, 1e9, 0.1, 6);
    CHECK(removed_demand_point_flow_editor.refreshUnits(context));
    QLineEdit *removed_demand_point_text =
        removed_demand_point_flow_editor.findChild<QLineEdit *>();
    CHECK(removed_demand_point_text != nullptr);
    removed_demand_point_text->selectAll();
    QKeyEvent removed_demand_point_input(QEvent::KeyPress, Qt::Key_unknown,
                                         Qt::NoModifier, QStringLiteral("2"));
    QApplication::sendEvent(removed_demand_point_text, &removed_demand_point_input);
    demand_rows_stale = true;
    demand_point_flows.pop_back();
    CHECK(removed_demand_point_flow_editor.commitEdit() ==
          NumericPropertyBinding::Result::Unavailable);
    CHECK(demand_point_writes == 1);
    CHECK(demand_point_flows[0] == 4.5);
    removed_demand_point_flow_editor.cancelEdit();

    // Reservoir terrain elevation, hydraulic-head offset, and total head can
    // each use different display units. Neither refresh nor an unchanged edit
    // may round-trip the precise source values through Qt's spin box.
    double terrain_m = 123.456789123456;
    double offset_m = 1.234567891234;
    double reservoir_head_m = 124.691357014690;
    const double original_terrain = terrain_m;
    const double original_offset = offset_m;
    const double original_head = reservoir_head_m;
    int reservoir_writes = 0;
    UnitNumericSpinBox terrain_editor(
        [&] { return std::optional<double>(terrain_m); },
        [&](double value) { terrain_m = value; ++reservoir_writes; return true; },
        "elevation", "m", -10000.0, 10000.0, 0.1, 3);
    UnitNumericSpinBox offset_editor(
        [&] { return std::optional<double>(offset_m); },
        [&](double value) { offset_m = value; ++reservoir_writes; return true; },
        "vertical_offset", "m", -10000.0, 10000.0, 0.1, 3);
    UnitNumericSpinBox head_editor(
        [&] { return std::optional<double>(reservoir_head_m); },
        [&](double value) { reservoir_head_m = value; ++reservoir_writes; return true; },
        "hydraulic_head", "m", -10000.0, 10000.0, 0.1, 3);
    context.update(QJsonObject{{QStringLiteral("elevation"), QStringLiteral("m")},
                               {QStringLiteral("vertical_offset"), QStringLiteral("ft")},
                               {QStringLiteral("hydraulic_head"), QStringLiteral("ft")}});
    CHECK(terrain_editor.refreshUnits(context));
    CHECK(offset_editor.refreshUnits(context));
    CHECK(head_editor.refreshUnits(context));
    CHECK(terrain_editor.suffix() == NumericText::suffix(QStringLiteral("m")));
    CHECK(offset_editor.suffix() == NumericText::suffix(QStringLiteral("ft")));
    CHECK(head_editor.suffix() == NumericText::suffix(QStringLiteral("ft")));
    CHECK(std::abs(offset_editor.value() - offset_m / 0.3048) < 0.00051);
    CHECK(std::abs(head_editor.value() - reservoir_head_m / 0.3048) < 0.00051);
    CHECK(head_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    context.update(QJsonObject{{QStringLiteral("elevation"), QStringLiteral("ft")},
                               {QStringLiteral("vertical_offset"), QStringLiteral("m")},
                               {QStringLiteral("hydraulic_head"), QStringLiteral("m")}});
    CHECK(terrain_editor.refreshUnits(context));
    CHECK(offset_editor.refreshUnits(context));
    CHECK(head_editor.refreshUnits(context));
    CHECK(reservoir_writes == 0 && terrain_m == original_terrain &&
          offset_m == original_offset && reservoir_head_m == original_head);

    // Tank level editors must preserve the three independent canonical values
    // across unit changes, including when their GUI text has fewer decimals.
    double tank_levels_m[] = {7.1234567890123, 0.01234567890123, 19.9876543210987};
    const double original_tank_levels_m[] = {
        tank_levels_m[0], tank_levels_m[1], tank_levels_m[2]
    };
    int tank_level_writes = 0;
    UnitNumericSpinBox tank_initial(
        [&] { return std::optional<double>(tank_levels_m[0]); },
        [&](double metres) { tank_levels_m[0] = metres; ++tank_level_writes; return true; },
        "tank_initial_level", "m", -1000000.0, 1000000.0, 0.1, 3);
    UnitNumericSpinBox tank_minimum(
        [&] { return std::optional<double>(tank_levels_m[1]); },
        [&](double metres) { tank_levels_m[1] = metres; ++tank_level_writes; return true; },
        "tank_minimum_level", "m", -1000000.0, 1000000.0, 0.1, 3);
    UnitNumericSpinBox tank_maximum(
        [&] { return std::optional<double>(tank_levels_m[2]); },
        [&](double metres) { tank_levels_m[2] = metres; ++tank_level_writes; return true; },
        "tank_maximum_level", "m", -1000000.0, 1000000.0, 0.1, 3);
    context.update(QJsonObject{{QStringLiteral("water_level"), QStringLiteral("ft")}});
    CHECK(tank_initial.refreshUnits(context));
    CHECK(tank_minimum.refreshUnits(context));
    CHECK(tank_maximum.refreshUnits(context));
    CHECK(tank_initial.suffix() == NumericText::suffix(QStringLiteral("ft")));
    CHECK(tank_minimum.suffix() == NumericText::suffix(QStringLiteral("ft")));
    CHECK(tank_maximum.suffix() == NumericText::suffix(QStringLiteral("ft")));
    // A tank can explicitly display its minimum level in metres while its
    // initial/maximum levels continue inheriting the legacy feet setting.
    QJsonObject independent_tank_level = context.effectiveUnits();
    independent_tank_level.insert(QStringLiteral("tank_minimum_level"), QStringLiteral("m"));
    CHECK(context.update(independent_tank_level));
    CHECK(tank_initial.refreshUnits(context));
    CHECK(tank_minimum.refreshUnits(context));
    CHECK(tank_maximum.refreshUnits(context));
    CHECK(tank_initial.suffix() == NumericText::suffix(QStringLiteral("ft")));
    CHECK(tank_minimum.suffix() == NumericText::suffix(QStringLiteral("m")));
    CHECK(tank_maximum.suffix() == NumericText::suffix(QStringLiteral("ft")));
    CHECK(tank_minimum.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(tank_level_writes == 0);
    independent_tank_level.remove(QStringLiteral("tank_minimum_level"));
    CHECK(context.update(independent_tank_level));
    CHECK(tank_minimum.refreshUnits(context));
    CHECK(tank_minimum.suffix() == NumericText::suffix(QStringLiteral("ft")));
    context.update(QJsonObject{{QStringLiteral("water_level"), QStringLiteral("m")}});
    CHECK(tank_initial.refreshUnits(context));
    CHECK(tank_minimum.refreshUnits(context));
    CHECK(tank_maximum.refreshUnits(context));
    CHECK(tank_level_writes == 0);
    CHECK(tank_levels_m[0] == original_tank_levels_m[0]);
    CHECK(tank_levels_m[1] == original_tank_levels_m[1]);
    CHECK(tank_levels_m[2] == original_tank_levels_m[2]);
    // Actual typed edits still commit after conversion into canonical metres.
    context.update(QJsonObject{{QStringLiteral("water_level"), QStringLiteral("ft")}});
    CHECK(tank_initial.refreshUnits(context));
    QLineEdit *tank_text = tank_initial.findChild<QLineEdit *>();
    CHECK(tank_text != nullptr);
    tank_text->selectAll();
    QKeyEvent tank_input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QStringLiteral("10"));
    QApplication::sendEvent(tank_text, &tank_input);
    CHECK(tank_initial.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(tank_levels_m[0] - 3.048) < 1e-12);
    CHECK(tank_levels_m[1] == original_tank_levels_m[1]);
    CHECK(tank_levels_m[2] == original_tank_levels_m[2]);
    CHECK(tank_level_writes == 1);

    // Tank geometry uses different quantity IDs for diameter, area and volume.
    // Derived geometry is displayed after conversion, never committed, and an
    // editor that became derived cannot write even if it retains a stale edit.
    double tank_geometry[] = {3.1234567890123, 7.6543210987654,
                              12.3456789012345, 105.678901234567};
    const double original_tank_geometry[] = {tank_geometry[0], tank_geometry[1],
                                             tank_geometry[2], tank_geometry[3]};
    int geometry_mode = 0; // cylindrical / uniform area / maximum volume / curve
    int geometry_writes = 0;
    const std::function<std::unique_ptr<UnitNumericSpinBox>(
        int, const std::string &, const std::string &)> geometry_editor =
        [&](int field, const std::string &quantity,
                                     const std::string &storage) {
        return std::make_unique<UnitNumericSpinBox>(
            [&, field] { return std::optional<double>(tank_geometry[field]); },
            [&, field](double value) {
                if ((field == 0 && geometry_mode != 0) ||
                    (field == 1 && geometry_mode != 1) ||
                    (field == 2 && geometry_mode == 3) ||
                    (field == 3 && geometry_mode != 2)) return false;
                tank_geometry[field] = value;
                ++geometry_writes;
                return true;
            }, quantity, storage, -1000000.0, 1000000.0, 1.0, 3);
    };
    std::unique_ptr<UnitNumericSpinBox> tank_diameter = geometry_editor(0, "tank_diameter", "m");
    std::unique_ptr<UnitNumericSpinBox> tank_area = geometry_editor(1, "area", "m2");
    std::unique_ptr<UnitNumericSpinBox> tank_min_volume = geometry_editor(2, "tank_minimum_volume", "m3");
    std::unique_ptr<UnitNumericSpinBox> tank_max_volume = geometry_editor(3, "volume", "m3");
    context.update(QJsonObject{{QStringLiteral("tank_diameter"), QStringLiteral("ft")},
                               {QStringLiteral("area"), QStringLiteral("ft2")},
                               {QStringLiteral("volume"), QStringLiteral("ft3")}});
    CHECK(tank_diameter->refreshUnits(context));
    CHECK(tank_area->refreshUnits(context));
    CHECK(tank_min_volume->refreshUnits(context));
    CHECK(tank_max_volume->refreshUnits(context));
    CHECK(tank_diameter->suffix() == NumericText::suffix(QStringLiteral("ft")));
    CHECK(tank_area->suffix() == NumericText::suffix(QStringLiteral("ft2")));
    CHECK(tank_min_volume->suffix() == NumericText::suffix(QStringLiteral("ft3")));
    CHECK(tank_max_volume->suffix() == NumericText::suffix(QStringLiteral("ft3")));
    // Even if an old profile still supplies a separate minimum-volume unit,
    // both tank volume controls must follow the general volume choice.
    QJsonObject independent_volume = context.effectiveUnits();
    independent_volume.insert(QStringLiteral("tank_minimum_volume"), QStringLiteral("m3"));
    CHECK(context.update(independent_volume));
    CHECK(tank_min_volume->refreshUnits(context));
    CHECK(tank_max_volume->refreshUnits(context));
    CHECK(tank_min_volume->suffix() == NumericText::suffix(QStringLiteral("ft3")));
    CHECK(tank_max_volume->suffix() == NumericText::suffix(QStringLiteral("ft3")));
    CHECK(tank_geometry[2] == original_tank_geometry[2]);
    CHECK(geometry_writes == 0);
    independent_volume.remove(QStringLiteral("tank_minimum_volume"));
    CHECK(context.update(independent_volume));
    CHECK(tank_min_volume->refreshUnits(context));
    CHECK(tank_min_volume->suffix() == NumericText::suffix(QStringLiteral("ft3")));
    const double derived_area_m2 = 3.14159265358979323846 * tank_geometry[0] * tank_geometry[0] / 4.0;
    tank_area->setReadOnly(true);
    CHECK(tank_area->refreshDerived(context, derived_area_m2));
    const double derived_area_ft2 = tank_area->value();
    CHECK(std::abs(derived_area_ft2 - derived_area_m2 / (0.3048 * 0.3048)) < 0.00051);
    CHECK(tank_area->commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(tank_area->value() == derived_area_ft2);
    tank_area->cancelEdit();
    CHECK(tank_area->value() == derived_area_ft2);
    tank_max_volume->setReadOnly(true);
    CHECK(tank_max_volume->refreshDerived(context, 43.1234567890123));
    const double derived_volume_ft3 = tank_max_volume->value();
    CHECK(std::abs(derived_volume_ft3 - 43.1234567890123 / (0.3048 * 0.3048 * 0.3048)) < 0.00051);
    CHECK(tank_max_volume->commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(tank_max_volume->value() == derived_volume_ft3);
    CHECK(!tank_max_volume->refreshDerived(context, std::nullopt));
    CHECK(tank_max_volume->specialValueText() == QStringLiteral("[Not available]"));
    CHECK(geometry_writes == 0);
    for (int index = 0; index < 4; ++index)
        CHECK(tank_geometry[index] == original_tank_geometry[index]);

    geometry_mode = 1;
    tank_diameter->setReadOnly(true);
    CHECK(tank_diameter->refreshDerived(context, 5.876543210987));
    tank_area->setReadOnly(false);
    tank_area->setSpecialValueText(QString());
    CHECK(tank_area->refreshUnits(context));
    CHECK(tank_area->commitEdit() == NumericPropertyBinding::Result::Unchanged);
    geometry_mode = 2;
    tank_max_volume->setReadOnly(false);
    tank_max_volume->setSpecialValueText(QString());
    CHECK(tank_max_volume->refreshUnits(context));
    CHECK(tank_max_volume->isEnabled());
    CHECK(std::abs(tank_max_volume->value() -
                   tank_geometry[3] / (0.3048 * 0.3048 * 0.3048)) < 0.00051);
    // A real typed edit of the maximum volume in ft3 must commit cubic metres.
    QLineEdit *volume_text = tank_max_volume->findChild<QLineEdit *>();
    CHECK(volume_text != nullptr);
    volume_text->selectAll();
    QKeyEvent volume_input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QStringLiteral("100"));
    QApplication::sendEvent(volume_text, &volume_input);
    CHECK(tank_max_volume->commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(tank_geometry[3] - 100.0 * 0.3048 * 0.3048 * 0.3048) < 1e-12);
    CHECK(geometry_writes == 1);
    for (int index = 0; index < 3; ++index)
        CHECK(tank_geometry[index] == original_tank_geometry[index]);

    // When a field is no longer the selected geometry input, its writer rejects
    // a stale edit rather than overwriting a derived/canonical measurement.
    geometry_mode = 0;
    tank_diameter->setReadOnly(false);
    CHECK(tank_diameter->refreshUnits(context));
    QLineEdit *diameter_text = tank_diameter->findChild<QLineEdit *>();
    CHECK(diameter_text != nullptr);
    diameter_text->selectAll();
    QKeyEvent diameter_input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QStringLiteral("20"));
    QApplication::sendEvent(diameter_text, &diameter_input);
    geometry_mode = 1;
    CHECK(tank_diameter->commitEdit() == NumericPropertyBinding::Result::Rejected);
    tank_diameter->cancelEdit();
    CHECK(geometry_writes == 1);
    CHECK(tank_geometry[0] == original_tank_geometry[0]);
    context.update(QJsonObject{{QStringLiteral("tank_diameter"), QStringLiteral("m")},
                               {QStringLiteral("area"), QStringLiteral("m2")},
                               {QStringLiteral("volume"), QStringLiteral("m3")}});
    CHECK(tank_diameter->refreshUnits(context));
    CHECK(tank_area->refreshUnits(context));
    CHECK(tank_min_volume->refreshUnits(context));
    CHECK(tank_max_volume->refreshUnits(context));
    CHECK(geometry_writes == 1);

    // Tank quality: a dimensionless mixing fraction can have more precision
    // in the Model than the spin box displays, without losing those digits.
    context.setLocale(QLocale::c()); // Decimal input in this regression test uses a dot.
    double mixing_fraction = 0.123456789123456;
    const double original_mixing = mixing_fraction;
    bool two_compartment = true;
    int quality_writes = 0;
    UnitNumericSpinBox fraction_editor(
        [&] { return std::optional<double>(mixing_fraction); },
        [&](double value) {
            if (!two_compartment || value < 0.0 || value > 1.0) return false;
            mixing_fraction = value;
            ++quality_writes;
            return true;
        }, "mixing_fraction", "1", 0.0, 1.0, 0.05, 4, nullptr,
        [](const QString &) { return QString(); });
    context.update(QJsonObject{{QStringLiteral("mixing_fraction"), QStringLiteral("1")},
                               {QStringLiteral("first_order_bulk_reaction_coefficient"), QStringLiteral("/d")}});
    CHECK(fraction_editor.refreshUnits(context));
    CHECK(fraction_editor.suffix().isEmpty());
    CHECK(fraction_editor.value() == 0.1235);
    CHECK(fraction_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(quality_writes == 0 && mixing_fraction == original_mixing);

    // EPANET bulk-reaction order controls the concentration dimension.
    // Changing from /d to /h or /s must scale *only* inverse time.
    double coefficient_per_day = -0.123456789012345;
    const double original_coefficient = coefficient_per_day;
    double reaction_order = 1.0;
    bool override_bulk_reaction = true;
    UnitNumericSpinBox coefficient_editor(
        [&] { return std::optional<double>(coefficient_per_day); },
        [&](double value) {
            if (!override_bulk_reaction) return false;
            coefficient_per_day = value;
            ++quality_writes;
            return true;
        }, "first_order_bulk_reaction_coefficient", "/d", -1000000.0, 1000000.0,
        0.001, 12, nullptr,
        [&](const QString &unit) { return UnitBulkReactionDisplay::suffix(reaction_order, unit); });
    CHECK(coefficient_editor.refreshUnits(context));
    CHECK(coefficient_editor.suffix() == NumericText::suffix(QStringLiteral("/d")));
    reaction_order = 0.0;
    CHECK(coefficient_editor.refreshUnits(context));
    CHECK(coefficient_editor.suffix() == NumericText::suffix(QStringLiteral("(mg/L)/d")));
    context.update(QJsonObject{{QStringLiteral("mixing_fraction"), QStringLiteral("1")},
                               {QStringLiteral("first_order_bulk_reaction_coefficient"), QStringLiteral("/h")}});
    CHECK(coefficient_editor.refreshUnits(context));
    CHECK(coefficient_editor.suffix() == NumericText::suffix(QStringLiteral("(mg/L)/h")));
    CHECK(std::abs(coefficient_editor.value() - original_coefficient / 24.0) < 1.0e-12);
    reaction_order = -1.0;
    CHECK(coefficient_editor.refreshUnits(context));
    CHECK(coefficient_editor.suffix() == NumericText::suffix(QStringLiteral("(mg/L)/h")));
    reaction_order = 2.0;
    CHECK(coefficient_editor.refreshUnits(context));
    CHECK(coefficient_editor.suffix() == NumericText::suffix(QStringLiteral("(mg/L)^-1/h")));
    CHECK(coefficient_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(quality_writes == 0 && coefficient_per_day == original_coefficient);
    context.update(QJsonObject{{QStringLiteral("mixing_fraction"), QStringLiteral("1")},
                               {QStringLiteral("first_order_bulk_reaction_coefficient"), QStringLiteral("/s")}});
    CHECK(coefficient_editor.refreshUnits(context));
    CHECK(coefficient_editor.suffix() == NumericText::suffix(QStringLiteral("(mg/L)^-1/s")));
    CHECK(std::abs(coefficient_editor.value() - original_coefficient / 86400.0) < 5e-13);
    context.update(QJsonObject{{QStringLiteral("mixing_fraction"), QStringLiteral("1")},
                               {QStringLiteral("first_order_bulk_reaction_coefficient"), QStringLiteral("/d")}});
    CHECK(coefficient_editor.refreshUnits(context));
    CHECK(coefficient_per_day == original_coefficient && quality_writes == 0);

    // Explicit user input, unlike refresh, converts to the canonical day basis.
    QLineEdit *quality_text = coefficient_editor.findChild<QLineEdit *>();
    CHECK(quality_text != nullptr);
    quality_text->selectAll();
    QKeyEvent quality_input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QStringLiteral("-0.5"));
    QApplication::sendEvent(quality_text, &quality_input);
    CHECK(coefficient_editor.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(coefficient_per_day == -0.5 && quality_writes == 1);

    // Disabling the override while an edit is pending must reject the stale
    // coefficient instead of writing it into a non-editable tank property.
    quality_text->selectAll();
    QKeyEvent stale_reaction_input(QEvent::KeyPress, Qt::Key_unknown,
                                   Qt::NoModifier, QStringLiteral("-0.75"));
    QApplication::sendEvent(quality_text, &stale_reaction_input);
    override_bulk_reaction = false;
    CHECK(coefficient_editor.commitEdit() == NumericPropertyBinding::Result::Rejected);
    coefficient_editor.cancelEdit();
    CHECK(coefficient_per_day == -0.5 && quality_writes == 1);

    // A real mixing-fraction edit may replace the precise original value.
    QLineEdit *fraction_text = fraction_editor.findChild<QLineEdit *>();
    CHECK(fraction_text != nullptr);
    fraction_text->selectAll();
    QKeyEvent mixing_input(QEvent::KeyPress, Qt::Key_unknown,
                            Qt::NoModifier, QStringLiteral("0.75"));
    QApplication::sendEvent(fraction_text, &mixing_input);
    CHECK(fraction_editor.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(mixing_fraction == 0.75 && quality_writes == 2);
    // Pipe geometry: link diameters use millimetres, but lengths use metres.
    // Changing the display units or drawing a derived length is read-only.
    double pipe_diameter_mm = 63.1234567890123;
    double pipe_calculated_m = 123.456789012345;
    std::optional<double> pipe_measured_m;
    const double original_pipe_diameter = pipe_diameter_mm;
    const double original_calculated = pipe_calculated_m;
    int pipe_writes = 0;
    UnitNumericSpinBox pipe_diameter(
        [&] { return std::optional<double>(pipe_diameter_mm); },
        [&](double millimetres) {
            pipe_diameter_mm = millimetres;
            ++pipe_writes;
            return true;
        }, "link_diameter", "mm", 1.0, 5000.0, 10.0, 2);
    UnitNumericSpinBox pipe_calculated(
        [&] { return std::optional<double>(pipe_calculated_m); },
        [&](double) { ++pipe_writes; return false; },
        "length", "m", 0.0, 1.0e9, 1.0, 2);
    UnitNumericSpinBox pipe_measured(
        [&] { return std::optional<double>(pipe_measured_m.value_or(pipe_calculated_m)); },
        [&](double metres) {
            if (!pipe_measured_m) return false;
            pipe_measured_m = metres;
            ++pipe_writes;
            return true;
        }, "length", "m", 0.0, 1.0e9, 1.0, 2);
    pipe_calculated.setReadOnly(true);
    pipe_measured.setReadOnly(true);
    context.update(QJsonObject{{QStringLiteral("link_diameter"), QStringLiteral("in")},
                               {QStringLiteral("length"), QStringLiteral("ft")}});
    CHECK(pipe_diameter.refreshUnits(context));
    CHECK(pipe_calculated.refreshDerived(context, pipe_calculated_m));
    CHECK(pipe_measured.refreshDerived(context, pipe_calculated_m));
    pipe_measured.setEnabled(false);
    CHECK(pipe_diameter.suffix() == NumericText::suffix(QStringLiteral("in")));
    CHECK(pipe_calculated.suffix() == NumericText::suffix(QStringLiteral("ft")));
    CHECK(pipe_measured.suffix() == NumericText::suffix(QStringLiteral("ft")));
    CHECK(pipe_calculated.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    const double derived_ft = pipe_calculated.value();
    pipe_calculated.cancelEdit();
    CHECK(pipe_calculated.value() == derived_ft);
    CHECK(pipe_writes == 0);
    CHECK(pipe_diameter_mm == original_pipe_diameter);
    CHECK(pipe_calculated_m == original_calculated);
    CHECK(!pipe_measured_m.has_value());

    pipe_measured_m = 44.1234567890123;
    const double original_measured = *pipe_measured_m;
    pipe_measured.setReadOnly(false);
    CHECK(pipe_measured.refreshUnits(context));
    CHECK(pipe_measured.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    context.update(QJsonObject{{QStringLiteral("link_diameter"), QStringLiteral("mm")},
                               {QStringLiteral("length"), QStringLiteral("m")}});
    CHECK(pipe_diameter.refreshUnits(context));
    CHECK(pipe_calculated.refreshDerived(context, pipe_calculated_m));
    CHECK(pipe_measured.refreshUnits(context));
    CHECK(pipe_writes == 0);
    CHECK(pipe_diameter_mm == original_pipe_diameter);
    CHECK(pipe_calculated_m == original_calculated);
    CHECK(pipe_measured_m == original_measured);

    context.update(QJsonObject{{QStringLiteral("link_diameter"), QStringLiteral("in")},
                               {QStringLiteral("length"), QStringLiteral("ft")}});
    CHECK(pipe_diameter.refreshUnits(context));
    CHECK(pipe_measured.refreshUnits(context));
    QLineEdit *pipe_diameter_text = pipe_diameter.findChild<QLineEdit *>();
    CHECK(pipe_diameter_text != nullptr);
    pipe_diameter_text->selectAll();
    QKeyEvent pipe_diameter_input(QEvent::KeyPress, Qt::Key_unknown,
                                   Qt::NoModifier, QStringLiteral("2"));
    QApplication::sendEvent(pipe_diameter_text, &pipe_diameter_input);
    CHECK(pipe_diameter.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(pipe_diameter_mm - 50.8) < 1e-11);
    CHECK(pipe_writes == 1);

    QLineEdit *pipe_measured_text = pipe_measured.findChild<QLineEdit *>();
    CHECK(pipe_measured_text != nullptr);
    pipe_measured_text->selectAll();
    QKeyEvent pipe_measured_input(QEvent::KeyPress, Qt::Key_unknown,
                                   Qt::NoModifier, QStringLiteral("50"));
    QApplication::sendEvent(pipe_measured_text, &pipe_measured_input);
    CHECK(pipe_measured.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(*pipe_measured_m - 15.24) < 1e-11);
    CHECK(pipe_writes == 2);

    // An override removed during a pending edit must not be recreated by
    // the stale editor when it attempts to commit.
    pipe_measured_text->selectAll();
    QKeyEvent stale_pipe_input(QEvent::KeyPress, Qt::Key_unknown,
                                Qt::NoModifier, QStringLiteral("60"));
    QApplication::sendEvent(pipe_measured_text, &stale_pipe_input);
    pipe_measured_m.reset();
    CHECK(pipe_measured.commitEdit() != NumericPropertyBinding::Result::Committed);
    pipe_measured.setReadOnly(true);
    CHECK(pipe_measured.refreshDerived(context, pipe_calculated_m));
    pipe_measured.setEnabled(false);
    CHECK(pipe_writes == 2);
    CHECK(!pipe_measured_m.has_value());
    CHECK(pipe_calculated_m == original_calculated);

    // Pipe roughness: only Darcy-Weisbach absolute roughness has a physical
    // length unit. The H-W, C-M and minor-loss values stay dimensionless.
    // All four preserve their higher-precision stored values on refresh.
    double roughness_values[] = {137.1234567890123, 0.123456789123456,
                                 0.0145678912345, 0.1256789123456};
    const double original_roughness[] = {roughness_values[0], roughness_values[1],
                                         roughness_values[2], roughness_values[3]};
    int roughness_writes = 0;
    bool material_mode = false;
    int active_roughness_formula = 1; // 0 H-W, 1 D-W, 2 C-M
    const std::function<std::unique_ptr<UnitNumericSpinBox>(
        int, const std::string &, const std::string &, double, double, int)> roughness_editor =
        [&](int field, const std::string &quantity,
                                      const std::string &storage, double minimum,
                                      double maximum, int decimals) {
        return std::make_unique<UnitNumericSpinBox>(
            [&, field] { return std::optional<double>(roughness_values[field]); },
            [&, field](double value) {
                if ((field < 3 && (material_mode || active_roughness_formula != field)))
                    return false;
                roughness_values[field] = value;
                ++roughness_writes;
                return true;
            }, quantity, storage, minimum, maximum, 0.001, decimals, nullptr,
            field == 1 ? std::function<QString(const QString &)>()
                       : std::function<QString(const QString &)>(
                             [](const QString &) { return QString(); }));
    };
    std::unique_ptr<UnitNumericSpinBox> roughness_hw = roughness_editor(
        0, "hazen_williams_roughness_coefficient", "1", 1.0, 200.0, 0);
    std::unique_ptr<UnitNumericSpinBox> roughness_dw = roughness_editor(
        1, "darcy_weisbach_roughness_height", "mm", 0.000001, 100.0, 6);
    std::unique_ptr<UnitNumericSpinBox> roughness_cm = roughness_editor(
        2, "chezy_manning_roughness_coefficient", "1", 0.001, 0.1, 4);
    std::unique_ptr<UnitNumericSpinBox> minor_loss = roughness_editor(
        3, "minor_loss_coefficient", "1", 0.0, 1000.0, 3);
    context.update(QJsonObject{
        {QStringLiteral("hazen_williams_roughness_coefficient"), QStringLiteral("1")},
        {QStringLiteral("darcy_weisbach_roughness_height"), QStringLiteral("in")},
        {QStringLiteral("chezy_manning_roughness_coefficient"), QStringLiteral("1")},
        {QStringLiteral("minor_loss_coefficient"), QStringLiteral("1")}});
    CHECK(roughness_hw->refreshUnits(context));
    CHECK(roughness_dw->refreshUnits(context));
    CHECK(roughness_cm->refreshUnits(context));
    CHECK(minor_loss->refreshUnits(context));
    CHECK(roughness_hw->suffix().isEmpty());
    CHECK(roughness_cm->suffix().isEmpty());
    CHECK(minor_loss->suffix().isEmpty());
    CHECK(roughness_dw->suffix() == NumericText::suffix(QStringLiteral("in")));
    CHECK(std::abs(roughness_dw->value() - original_roughness[1] / 25.4) < 1e-6);
    CHECK(roughness_hw->commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(roughness_dw->commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(roughness_cm->commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(minor_loss->commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(roughness_writes == 0);
    for (int index = 0; index < 4; ++index)
        CHECK(roughness_values[index] == original_roughness[index]);

    context.update(QJsonObject{
        {QStringLiteral("hazen_williams_roughness_coefficient"), QStringLiteral("1")},
        {QStringLiteral("darcy_weisbach_roughness_height"), QStringLiteral("millift")},
        {QStringLiteral("chezy_manning_roughness_coefficient"), QStringLiteral("1")},
        {QStringLiteral("minor_loss_coefficient"), QStringLiteral("1")}});
    CHECK(roughness_dw->refreshUnits(context));
    CHECK(roughness_dw->suffix() == NumericText::suffix(QStringLiteral("millift")));
    CHECK(std::abs(roughness_dw->value() - original_roughness[1] / 0.3048) < 1e-6);
    CHECK(roughness_writes == 0 && roughness_values[1] == original_roughness[1]);

    // The material library supplies a derived value, not a new explicit
    // roughness. Focus, Escape, Enter and profile updates must never persist it.
    material_mode = true;
    roughness_dw->setReadOnly(true);
    CHECK(roughness_dw->refreshDerived(context, 0.087654321987));
    const double derived_roughness = roughness_dw->value();
    CHECK(std::abs(derived_roughness - 0.087654321987 / 0.3048) < 1e-6);
    CHECK(roughness_dw->commitEdit() == NumericPropertyBinding::Result::Unchanged);
    roughness_dw->cancelEdit();
    CHECK(roughness_dw->value() == derived_roughness);
    CHECK(!roughness_dw->refreshDerived(context, std::nullopt));
    CHECK(roughness_dw->specialValueText() == QStringLiteral("[Not available]"));
    CHECK(roughness_values[1] == original_roughness[1] && roughness_writes == 0);

    material_mode = false;
    roughness_dw->setReadOnly(false);
    roughness_dw->setSpecialValueText(QString());
    context.update(QJsonObject{
        {QStringLiteral("hazen_williams_roughness_coefficient"), QStringLiteral("1")},
        {QStringLiteral("darcy_weisbach_roughness_height"), QStringLiteral("in")},
        {QStringLiteral("chezy_manning_roughness_coefficient"), QStringLiteral("1")},
        {QStringLiteral("minor_loss_coefficient"), QStringLiteral("1")}});
    CHECK(roughness_dw->refreshUnits(context));
    QLineEdit *roughness_text = roughness_dw->findChild<QLineEdit *>();
    CHECK(roughness_text != nullptr);
    roughness_text->selectAll();
    QKeyEvent roughness_input(QEvent::KeyPress, Qt::Key_unknown,
                              Qt::NoModifier, QStringLiteral("0.01"));
    QApplication::sendEvent(roughness_text, &roughness_input);
    CHECK(roughness_dw->commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(roughness_values[1] - 0.254) < 1e-12);
    CHECK(roughness_writes == 1);
    CHECK(roughness_values[0] == original_roughness[0]);
    CHECK(roughness_values[2] == original_roughness[2]);
    CHECK(roughness_values[3] == original_roughness[3]);

    // A pending edit cannot overwrite an inactive roughness formula.
    roughness_text->selectAll();
    QKeyEvent stale_roughness(QEvent::KeyPress, Qt::Key_unknown,
                              Qt::NoModifier, QStringLiteral("0.02"));
    QApplication::sendEvent(roughness_text, &stale_roughness);
    active_roughness_formula = 0;
    CHECK(roughness_dw->commitEdit() == NumericPropertyBinding::Result::Rejected);
    roughness_dw->cancelEdit();
    CHECK(roughness_values[1] == 0.254 && roughness_writes == 1);

    // The two remaining roughness coefficients accept deliberate edits
    // without a suffix or unintended unit scaling.
    material_mode = false;
    active_roughness_formula = 0;
    CHECK(roughness_hw->refreshUnits(context));
    QLineEdit *hw_text = roughness_hw->findChild<QLineEdit *>();
    CHECK(hw_text != nullptr);
    hw_text->selectAll();
    QKeyEvent hw_input(QEvent::KeyPress, Qt::Key_unknown,
                       Qt::NoModifier, QStringLiteral("142"));
    QApplication::sendEvent(hw_text, &hw_input);
    CHECK(roughness_hw->commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(roughness_values[0] == 142.0 && roughness_writes == 2);

    active_roughness_formula = 2;
    CHECK(roughness_cm->refreshUnits(context));
    QLineEdit *cm_text = roughness_cm->findChild<QLineEdit *>();
    CHECK(cm_text != nullptr);
    cm_text->selectAll();
    QKeyEvent cm_input(QEvent::KeyPress, Qt::Key_unknown,
                       Qt::NoModifier, QStringLiteral("0.02"));
    QApplication::sendEvent(cm_text, &cm_input);
    CHECK(roughness_cm->commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(roughness_values[2] - 0.02) < 1e-12 && roughness_writes == 3);

    // Minor-loss K is always independently editable, including with the
    // material-library roughness mode selected.
    material_mode = true;
    CHECK(minor_loss->refreshUnits(context));
    QLineEdit *loss_text = minor_loss->findChild<QLineEdit *>();
    CHECK(loss_text != nullptr);
    loss_text->selectAll();
    QKeyEvent loss_input(QEvent::KeyPress, Qt::Key_unknown,
                         Qt::NoModifier, QStringLiteral("2.5"));
    QApplication::sendEvent(loss_text, &loss_input);
    CHECK(minor_loss->commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(roughness_values[3] == 2.5 && roughness_writes == 4);

    // Pipe bulk and wall kinetics: first-order wall and zero-order wall are
    // distinct physical quantities; neither can use the other's unit mapping.
    double pipe_bulk_per_day = -0.123456789012345;
    double pipe_wall_coefficient = -12.3456789012345;
    const double original_pipe_bulk_per_day = pipe_bulk_per_day;
    const double original_pipe_wall_coefficient = pipe_wall_coefficient;
    double pipe_bulk_order = 0.0;
    double pipe_wall_order = 1.0;
    bool pipe_override_bulk = true;
    bool pipe_override_wall = true;
    int reaction_writes = 0;
    UnitNumericSpinBox pipe_bulk_editor(
        [&] { return std::optional<double>(pipe_bulk_per_day); },
        [&](double value) {
            if (!pipe_override_bulk) return false;
            pipe_bulk_per_day = value;
            ++reaction_writes;
            return true;
        }, "first_order_bulk_reaction_coefficient", "/d", -1000000.0, 1000000.0,
        0.001, 12, nullptr,
        [&](const QString &unit) { return UnitBulkReactionDisplay::suffix(pipe_bulk_order, unit); });
    UnitNumericSpinBox pipe_wall_first(
        [&] { return std::optional<double>(pipe_wall_coefficient); },
        [&](double value) {
            if (!pipe_override_wall || pipe_wall_order != 1.0) return false;
            pipe_wall_coefficient = value;
            ++reaction_writes;
            return true;
        }, "first_order_wall_reaction_coefficient", "m/d", -1000000.0, 1000000.0,
        0.001, 12);
    UnitNumericSpinBox pipe_wall_zero(
        [&] { return std::optional<double>(pipe_wall_coefficient); },
        [&](double value) {
            if (!pipe_override_wall || pipe_wall_order != 0.0) return false;
            pipe_wall_coefficient = value;
            ++reaction_writes;
            return true;
        }, "zero_order_wall_reaction_coefficient", "mg/m2/d", -1000000.0, 1000000.0,
        0.001, 12);
    context.update(QJsonObject{
        {QStringLiteral("first_order_bulk_reaction_coefficient"), QStringLiteral("/h")},
        {QStringLiteral("first_order_wall_reaction_coefficient"), QStringLiteral("ft/d")},
        {QStringLiteral("zero_order_wall_reaction_coefficient"), QStringLiteral("mg/m2/h")}});
    CHECK(pipe_bulk_editor.refreshUnits(context));
    CHECK(pipe_wall_first.refreshUnits(context));
    CHECK(pipe_bulk_editor.suffix() == NumericText::suffix(QStringLiteral("(mg/L)/h")));
    CHECK(pipe_wall_first.suffix() == NumericText::suffix(QStringLiteral("ft/d")));
    CHECK(std::abs(pipe_bulk_editor.value() - original_pipe_bulk_per_day / 24.0) < 1e-10);
    CHECK(std::abs(pipe_wall_first.value() - original_pipe_wall_coefficient / 0.3048) < 1e-9);
    CHECK(pipe_bulk_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(pipe_wall_first.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(reaction_writes == 0);
    CHECK(pipe_bulk_per_day == original_pipe_bulk_per_day);
    CHECK(pipe_wall_coefficient == original_pipe_wall_coefficient);

    // Order changes invalidate first-order edits, even when its editor still
    // has uncommitted text; a zero-order editor uses mass/area/time instead.
    QLineEdit *wall_first_text = pipe_wall_first.findChild<QLineEdit *>();
    CHECK(wall_first_text != nullptr);
    wall_first_text->selectAll();
    QKeyEvent first_input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier,
                          QStringLiteral("10"));
    QApplication::sendEvent(wall_first_text, &first_input);
    pipe_wall_order = 0.0;
    CHECK(pipe_wall_first.commitEdit() == NumericPropertyBinding::Result::Rejected);
    pipe_wall_first.cancelEdit();
    CHECK(pipe_wall_zero.refreshUnits(context));
    CHECK(pipe_wall_zero.suffix() == NumericText::suffix(QStringLiteral("mg/m2/h")));
    CHECK(std::abs(pipe_wall_zero.value() - original_pipe_wall_coefficient / 24.0) < 1e-9);
    CHECK(pipe_wall_zero.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(reaction_writes == 0);
    CHECK(pipe_wall_coefficient == original_pipe_wall_coefficient);

    context.update(QJsonObject{
        {QStringLiteral("first_order_bulk_reaction_coefficient"), QStringLiteral("/s")},
        {QStringLiteral("first_order_wall_reaction_coefficient"), QStringLiteral("m/d")},
        {QStringLiteral("zero_order_wall_reaction_coefficient"), QStringLiteral("g/m2/d")}});
    CHECK(pipe_bulk_editor.refreshUnits(context));
    CHECK(pipe_wall_zero.refreshUnits(context));
    CHECK(pipe_wall_zero.suffix() == NumericText::suffix(QStringLiteral("g/m2/d")));
    CHECK(std::abs(pipe_wall_zero.value() - original_pipe_wall_coefficient / 1000.0) < 1e-10);
    CHECK(reaction_writes == 0);
    CHECK(pipe_wall_coefficient == original_pipe_wall_coefficient);
    QLineEdit *wall_zero_text = pipe_wall_zero.findChild<QLineEdit *>();
    CHECK(wall_zero_text != nullptr);
    wall_zero_text->selectAll();
    QKeyEvent zero_input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier,
                         QStringLiteral("0.015"));
    QApplication::sendEvent(wall_zero_text, &zero_input);
    CHECK(pipe_wall_zero.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(pipe_wall_coefficient - 15.0) < 1e-11);
    CHECK(reaction_writes == 1);

    // An override disabled after editing begins rejects rather than silently
    // writing an inactive reaction coefficient.
    wall_zero_text->selectAll();
    QKeyEvent stale_zero(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier,
                         QStringLiteral("0.022"));
    QApplication::sendEvent(wall_zero_text, &stale_zero);
    pipe_override_wall = false;
    CHECK(pipe_wall_zero.commitEdit() == NumericPropertyBinding::Result::Rejected);
    pipe_wall_zero.cancelEdit();
    CHECK(pipe_wall_coefficient == 15.0 && reaction_writes == 1);
    pipe_override_bulk = false;
    pipe_bulk_order = 2.0;
    CHECK(pipe_bulk_editor.refreshUnits(context));
    CHECK(pipe_bulk_editor.suffix() == NumericText::suffix(QStringLiteral("(mg/L)^-1/s")));
    CHECK(pipe_bulk_per_day == original_pipe_bulk_per_day && reaction_writes == 1);

    // Valve diameter and minor-loss coefficient are independent measurements.
    // Diameter may be displayed as inches, while K remains dimensionless.
    // Rounded widget values and unit switches must not rewrite the Model.
    double valve_diameter_mm = 123.456789012345;
    double valve_minor_loss = 0.123456789012345;
    const double original_valve_diameter = valve_diameter_mm;
    const double original_valve_loss = valve_minor_loss;
    int valve_writes = 0;
    bool valve_exists = true;
    UnitNumericSpinBox valve_diameter_editor(
        [&]() -> std::optional<double> {
            return valve_exists ? std::optional<double>(valve_diameter_mm) : std::nullopt;
        },
        [&](double mm) {
            if (!valve_exists) return false;
            valve_diameter_mm = mm;
            ++valve_writes;
            return true;
        }, "link_diameter", "mm", 0.0, 1000000.0, 10.0, 2);
    UnitNumericSpinBox valve_loss_editor(
        [&]() -> std::optional<double> {
            return valve_exists ? std::optional<double>(valve_minor_loss) : std::nullopt;
        },
        [&](double coefficient) {
            if (!valve_exists) return false;
            valve_minor_loss = coefficient;
            ++valve_writes;
            return true;
        }, "minor_loss_coefficient", "1", 0.0, 1000000.0, 0.1, 4);
    context.update(QJsonObject{
        {QStringLiteral("link_diameter"), QStringLiteral("in")},
        {QStringLiteral("minor_loss_coefficient"), QStringLiteral("1")}});
    CHECK(valve_diameter_editor.refreshUnits(context));
    CHECK(valve_loss_editor.refreshUnits(context));
    CHECK(valve_diameter_editor.suffix() == NumericText::suffix(QStringLiteral("in")));
    CHECK(valve_loss_editor.suffix() == NumericText::suffix(QStringLiteral("1")));
    CHECK(std::abs(valve_diameter_editor.value() - original_valve_diameter / 25.4) < 0.0051);
    CHECK(valve_diameter_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(valve_loss_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    context.update(QJsonObject{
        {QStringLiteral("link_diameter"), QStringLiteral("cm")},
        {QStringLiteral("minor_loss_coefficient"), QStringLiteral("1")}});
    CHECK(valve_diameter_editor.refreshUnits(context));
    CHECK(valve_loss_editor.refreshUnits(context));
    CHECK(valve_writes == 0);
    CHECK(valve_diameter_mm == original_valve_diameter);
    CHECK(valve_minor_loss == original_valve_loss);

    // An actual typed edit must commit in the selected display unit.
    context.update(QJsonObject{
        {QStringLiteral("link_diameter"), QStringLiteral("in")},
        {QStringLiteral("minor_loss_coefficient"), QStringLiteral("1")}});
    CHECK(valve_diameter_editor.refreshUnits(context));
    QLineEdit *valve_diameter_text = valve_diameter_editor.findChild<QLineEdit *>();
    CHECK(valve_diameter_text != nullptr);
    valve_diameter_text->selectAll();
    QKeyEvent valve_diameter_input(QEvent::KeyPress, Qt::Key_unknown,
                                    Qt::NoModifier, QStringLiteral("2"));
    QApplication::sendEvent(valve_diameter_text, &valve_diameter_input);
    CHECK(valve_diameter_editor.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(valve_diameter_mm - 50.8) < 1e-12 && valve_writes == 1);
    CHECK(valve_minor_loss == original_valve_loss);

    // A removed valve cannot accept an edit that was started before removal.
    QLineEdit *valve_loss_text = valve_loss_editor.findChild<QLineEdit *>();
    CHECK(valve_loss_text != nullptr);
    valve_loss_text->selectAll();
    QKeyEvent valve_loss_input(QEvent::KeyPress, Qt::Key_unknown,
                                Qt::NoModifier, QStringLiteral("2.5"));
    QApplication::sendEvent(valve_loss_text, &valve_loss_input);
    valve_exists = false;
    CHECK(valve_loss_editor.commitEdit() == NumericPropertyBinding::Result::Unavailable);
    CHECK(valve_writes == 1 && valve_minor_loss == original_valve_loss);
    valve_loss_editor.cancelEdit();

    // Type-specific valve settings have four distinct canonical properties:
    // pressure head in m, controlled flow in m3/h, dimensionless loss and
    // position in %. A rounded display or profile change cannot write any.
    HydraulicLinkValveType setting_type = HydraulicLinkValveType::PRV;
    double setting_head_m = 12.3456789012345;
    double setting_flow_m3h = 7.6543210987654;
    double setting_throttle = 2.3456789012345;
    double setting_position_percent = 37.1234567890123;
    const double original_setting_head = setting_head_m;
    const double original_flow = setting_flow_m3h;
    const double original_throttle = setting_throttle;
    const double original_position = setting_position_percent;
    int setting_writes = 0;
    const std::function<bool()> is_pressure_type = [&] {
        return setting_type == HydraulicLinkValveType::PRV ||
               setting_type == HydraulicLinkValveType::PSV ||
               setting_type == HydraulicLinkValveType::PBV;
    };
    UnitNumericSpinBox valve_pressure_setting(
        [&]() -> std::optional<double> {
            return valve_exists && is_pressure_type() ? std::optional<double>(setting_head_m) : std::nullopt;
        },
        [&](double metres) {
            if (!valve_exists || !is_pressure_type()) return false;
            setting_head_m = metres;
            ++setting_writes;
            return true;
        }, "pressure_head", "m", 0.0, 1000000.0, 1.0, 2);
    UnitNumericSpinBox valve_flow_setting(
        [&]() -> std::optional<double> {
            return valve_exists && setting_type == HydraulicLinkValveType::FCV
                ? std::optional<double>(setting_flow_m3h) : std::nullopt;
        },
        [&](double flow_m3h) {
            if (!valve_exists || setting_type != HydraulicLinkValveType::FCV) return false;
            setting_flow_m3h = flow_m3h;
            ++setting_writes;
            return true;
        }, "volumetric_flow_rate", "m3/h", 0.0, 1000000.0, 1.0, 3);
    UnitNumericSpinBox valve_throttle_setting(
        [&]() -> std::optional<double> {
            return valve_exists && setting_type == HydraulicLinkValveType::TCV
                ? std::optional<double>(setting_throttle) : std::nullopt;
        },
        [&](double loss) {
            if (!valve_exists || setting_type != HydraulicLinkValveType::TCV) return false;
            setting_throttle = loss;
            ++setting_writes;
            return true;
        }, "minor_loss_coefficient", "1", 0.0, 1000000.0, 0.1, 3,
        nullptr, [](const QString &) { return QString(); });
    UnitNumericSpinBox valve_position_setting(
        [&]() -> std::optional<double> {
            return valve_exists && setting_type == HydraulicLinkValveType::PCV
                ? std::optional<double>(setting_position_percent) : std::nullopt;
        },
        [&](double percent) {
            if (!valve_exists || setting_type != HydraulicLinkValveType::PCV) return false;
            setting_position_percent = percent;
            ++setting_writes;
            return true;
        }, "valve_position", "%", 0.0, 100.0, 1.0, 2,
        nullptr, [](const QString &unit) {
            return unit == QStringLiteral("1") ? QString() : NumericText::suffix(unit);
        });
    valve_exists = true;
    context.update(QJsonObject{
        {QStringLiteral("pressure_head"), QStringLiteral("ft")},
        {QStringLiteral("volumetric_flow_rate"), QStringLiteral("L/s")},
        {QStringLiteral("minor_loss_coefficient"), QStringLiteral("1")},
        {QStringLiteral("valve_position"), QStringLiteral("1")}});
    CHECK(valve_pressure_setting.refreshUnits(context));
    CHECK(valve_pressure_setting.suffix() == NumericText::suffix(QStringLiteral("ft")));
    CHECK(std::abs(valve_pressure_setting.value() - original_setting_head / 0.3048) < 0.0051);
    CHECK(valve_pressure_setting.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    context.update(QJsonObject{
        {QStringLiteral("pressure_head"), QStringLiteral("m")},
        {QStringLiteral("volumetric_flow_rate"), QStringLiteral("m3/h")},
        {QStringLiteral("minor_loss_coefficient"), QStringLiteral("1")},
        {QStringLiteral("valve_position"), QStringLiteral("%")}});
    CHECK(valve_pressure_setting.refreshUnits(context));
    CHECK(setting_writes == 0 && setting_head_m == original_setting_head);

    // Flow control converts L/s to the Model's m3/h and does not use the
    // pressure head editor's value (even when the valve type changes).
    setting_type = HydraulicLinkValveType::FCV;
    valve_pressure_setting.cancelEdit();
    context.update(QJsonObject{
        {QStringLiteral("pressure_head"), QStringLiteral("ft")},
        {QStringLiteral("volumetric_flow_rate"), QStringLiteral("L/s")},
        {QStringLiteral("minor_loss_coefficient"), QStringLiteral("1")},
        {QStringLiteral("valve_position"), QStringLiteral("1")}});
    CHECK(valve_flow_setting.refreshUnits(context));
    CHECK(valve_flow_setting.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(setting_writes == 0 && setting_flow_m3h == original_flow);
    QLineEdit *valve_flow_text = valve_flow_setting.findChild<QLineEdit *>();
    CHECK(valve_flow_text != nullptr);
    valve_flow_text->selectAll();
    QKeyEvent valve_flow_input(QEvent::KeyPress, Qt::Key_unknown,
                               Qt::NoModifier, QStringLiteral("2"));
    QApplication::sendEvent(valve_flow_text, &valve_flow_input);
    CHECK(valve_flow_setting.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(setting_flow_m3h - 7.2) < 1e-12 && setting_writes == 1);
    CHECK(setting_head_m == original_setting_head);

    // Changing FCV to a curve-based GPV during editing cannot write the old
    // flow setting; the GPV has no numeric setting editor.
    valve_flow_text->selectAll();
    QKeyEvent stale_flow_input(QEvent::KeyPress, Qt::Key_unknown,
                               Qt::NoModifier, QStringLiteral("4"));
    QApplication::sendEvent(valve_flow_text, &stale_flow_input);
    setting_type = HydraulicLinkValveType::GPV;
    CHECK(valve_flow_setting.commitEdit() == NumericPropertyBinding::Result::Unavailable);
    valve_flow_setting.cancelEdit();
    CHECK(setting_writes == 1 && std::abs(setting_flow_m3h - 7.2) < 1e-12);

    setting_type = HydraulicLinkValveType::TCV;
    CHECK(valve_throttle_setting.refreshUnits(context));
    CHECK(valve_throttle_setting.suffix().isEmpty());
    CHECK(valve_throttle_setting.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(setting_writes == 1 && setting_throttle == original_throttle);

    // Valve position is canonically a percentage, while the selected profile
    // may display an equivalent dimensionless ratio in the interval [0,1].
    setting_type = HydraulicLinkValveType::PCV;
    CHECK(valve_position_setting.refreshUnits(context));
    CHECK(valve_position_setting.suffix().isEmpty());
    CHECK(std::abs(valve_position_setting.value() - original_position / 100.0) < 0.0051);
    CHECK(valve_position_setting.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(setting_writes == 1 && setting_position_percent == original_position);
    QLineEdit *valve_position_text = valve_position_setting.findChild<QLineEdit *>();
    CHECK(valve_position_text != nullptr);
    valve_position_text->selectAll();
    QKeyEvent valve_position_input(QEvent::KeyPress, Qt::Key_unknown,
                                   Qt::NoModifier, QStringLiteral("0.6"));
    QApplication::sendEvent(valve_position_text, &valve_position_input);
    CHECK(valve_position_setting.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(setting_position_percent - 60.0) < 1e-12 && setting_writes == 2);
    CHECK(setting_throttle == original_throttle);

    setting_type = HydraulicLinkValveType::PBV;
    CHECK(valve_pressure_setting.refreshUnits(context));
    QLineEdit *valve_pressure_text = valve_pressure_setting.findChild<QLineEdit *>();
    CHECK(valve_pressure_text != nullptr);
    valve_pressure_text->selectAll();
    QKeyEvent valve_pressure_input(QEvent::KeyPress, Qt::Key_unknown,
                                   Qt::NoModifier, QStringLiteral("10"));
    QApplication::sendEvent(valve_pressure_text, &valve_pressure_input);
    CHECK(valve_pressure_setting.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(setting_head_m - 3.048) < 1e-12 && setting_writes == 3);
    CHECK(setting_throttle == original_throttle);

    // Pump constant power can use hp in a US-customary profile. Initial speed
    // remains a dimensionless ratio with a visual × suffix. Both can be
    // rounded in Qt without modifying their authoritative doubles.
    double pump_power_kw = 42.123456789123;
    double pump_speed_ratio = 1.234567891234;
    const double original_pump_power = pump_power_kw;
    const double original_pump_speed = pump_speed_ratio;
    int pump_writes = 0;
    bool pump_exists = true;
    bool constant_power_mode = true;
    UnitNumericSpinBox pump_power_editor(
        [&]() -> std::optional<double> {
            if (!pump_exists || !constant_power_mode) return std::nullopt;
            return pump_power_kw;
        },
        [&](double kilowatts) {
            if (!pump_exists || !constant_power_mode) return false;
            pump_power_kw = kilowatts;
            ++pump_writes;
            return true;
        }, "power", "kW", 0.0, 1000000.0, 1.0, 3);
    UnitNumericSpinBox pump_speed_editor(
        [&]() -> std::optional<double> {
            return pump_exists ? std::optional<double>(pump_speed_ratio) : std::nullopt;
        },
        [&](double speed_ratio) {
            if (!pump_exists) return false;
            pump_speed_ratio = speed_ratio;
            ++pump_writes;
            return true;
        }, "pump_speed_ratio", "1", 0.0, 10.0, 0.05, 2, nullptr,
        [](const QString &) { return QStringLiteral(" ×"); });
    context.update(QJsonObject{{QStringLiteral("power"), QStringLiteral("hp")},
                               {QStringLiteral("pump_speed_ratio"), QStringLiteral("1")}});
    CHECK(pump_power_editor.refreshUnits(context));
    CHECK(pump_speed_editor.refreshUnits(context));
    CHECK(pump_power_editor.suffix() == NumericText::suffix(QStringLiteral("hp")));
    CHECK(pump_speed_editor.suffix() == QStringLiteral(" ×"));
    CHECK(std::abs(pump_power_editor.value() -
          original_pump_power / aowis::units::kilowatts_per_mechanical_horsepower) < 0.00051);
    CHECK(pump_power_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(pump_speed_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    context.update(QJsonObject{{QStringLiteral("power"), QStringLiteral("W")},
                               {QStringLiteral("pump_speed_ratio"), QStringLiteral("1")}});
    CHECK(pump_power_editor.refreshUnits(context));
    CHECK(pump_speed_editor.refreshUnits(context));
    CHECK(pump_power_editor.suffix() == NumericText::suffix(QStringLiteral("W")));
    CHECK(pump_power_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(pump_power_kw == original_pump_power && pump_speed_ratio == original_pump_speed);
    CHECK(pump_writes == 0);

    // A typed horsepower edit commits the corresponding kW value, but a
    // pending power edit must be rejected once the pump switches away from
    // the constant-power definition type.
    context.update(QJsonObject{{QStringLiteral("power"), QStringLiteral("hp")},
                               {QStringLiteral("pump_speed_ratio"), QStringLiteral("1")}});
    CHECK(pump_power_editor.refreshUnits(context));
    QLineEdit *pump_power_text = pump_power_editor.findChild<QLineEdit *>();
    CHECK(pump_power_text != nullptr);
    pump_power_text->selectAll();
    QKeyEvent pump_power_input(QEvent::KeyPress, Qt::Key_unknown,
                                Qt::NoModifier, QStringLiteral("10"));
    QApplication::sendEvent(pump_power_text, &pump_power_input);
    CHECK(pump_power_editor.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(pump_power_kw - 10.0 * aowis::units::kilowatts_per_mechanical_horsepower) < 1e-12);
    CHECK(pump_writes == 1 && pump_speed_ratio == original_pump_speed);
    pump_power_text->selectAll();
    QKeyEvent stale_pump_power_input(QEvent::KeyPress, Qt::Key_unknown,
                                      Qt::NoModifier, QStringLiteral("20"));
    QApplication::sendEvent(pump_power_text, &stale_pump_power_input);
    constant_power_mode = false;
    CHECK(pump_power_editor.commitEdit() == NumericPropertyBinding::Result::Unavailable);
    pump_power_editor.cancelEdit();
    CHECK(!pump_power_editor.refreshUnits(context));
    CHECK(pump_writes == 1);
    constant_power_mode = true;
    CHECK(pump_power_editor.refreshUnits(context));

    QLineEdit *pump_speed_text = pump_speed_editor.findChild<QLineEdit *>();
    CHECK(pump_speed_text != nullptr);
    pump_speed_text->selectAll();
    QKeyEvent pump_speed_input(QEvent::KeyPress, Qt::Key_unknown,
                                Qt::NoModifier, QStringLiteral("1.5"));
    QApplication::sendEvent(pump_speed_text, &pump_speed_input);
    CHECK(pump_speed_editor.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(pump_speed_ratio == 1.5 && pump_writes == 2);
    CHECK(!pump_power_editor.isReadOnly());

    // Electricity tariffs are inverse-energy quantities: an amount of energy
    // multiplied by its price per energy unit has the same currency value for
    // every selected denominator. Currency identity is NEVER converted.
    const std::optional<double> eur_per_mj =
        UnitEnergyPriceConversion::fromStored(0.36, "MJ");
    const std::optional<double> eur_per_j =
        UnitEnergyPriceConversion::fromStored(0.36, "J");
    CHECK(eur_per_mj && std::abs(*eur_per_mj - 0.1) < 1e-15);
    CHECK(eur_per_j && std::abs(*eur_per_j - 1.0e-7) < 1e-20);
    const std::optional<double> restored_eur_per_kwh =
        UnitEnergyPriceConversion::toStored(*eur_per_mj, "MJ");
    CHECK(restored_eur_per_kwh && std::abs(*restored_eur_per_kwh - 0.36) < 1e-14);
    CHECK(!UnitEnergyPriceConversion::fromStored(1.0, "invalid"));
    CHECK(!UnitEnergyPriceConversion::toStored(-1.0, "MJ"));

    double pump_price_per_kwh = 0.271234567891234;
    const double original_pump_price = pump_price_per_kwh;
    int price_writes = 0;
    bool has_pump = true;
    bool constant_price_mode = true;
    UnitEnergyPriceSpinBox pump_price_editor(
        [&]() -> std::optional<double> {
            return has_pump ? std::optional<double>(pump_price_per_kwh) : std::nullopt;
        },
        [&](double amount) {
            if (!has_pump || !constant_price_mode) return false;
            pump_price_per_kwh = amount;
            ++price_writes;
            return true;
        });
    context.update(QJsonObject{{QStringLiteral("energy"), QStringLiteral("kW.h")}});
    CHECK(pump_price_editor.refreshUnits(context, QStringLiteral("EUR")));
    CHECK(pump_price_editor.suffix() == NumericText::suffix(QStringLiteral("EUR/kW.h")));
    CHECK(pump_price_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    context.update(QJsonObject{{QStringLiteral("energy"), QStringLiteral("MJ")}});
    CHECK(pump_price_editor.refreshUnits(context, QStringLiteral("XAF")));
    CHECK(pump_price_editor.suffix() == NumericText::suffix(QStringLiteral("XAF/MJ")));
    CHECK(std::abs(pump_price_editor.value() - original_pump_price / 3.6) < 0.000051);
    context.update(QJsonObject{{QStringLiteral("energy"), QStringLiteral("J")}});
    CHECK(pump_price_editor.refreshUnits(context, QStringLiteral("EUR")));
    CHECK(pump_price_editor.suffix() == NumericText::suffix(QStringLiteral("EUR/J")));
    CHECK(pump_price_editor.decimals() >= 12);
    CHECK(pump_price_editor.value() > 0.0); // Must never display a positive tariff as "Not priced".
    CHECK(pump_price_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(pump_price_per_kwh == original_pump_price && price_writes == 0);

    // A typed tariff expressed per MJ is translated to the canonical per-kWh
    // amount. Clicking/focusing with no edit, by contrast, is read-only.
    context.update(QJsonObject{{QStringLiteral("energy"), QStringLiteral("MJ")}});
    CHECK(pump_price_editor.refreshUnits(context, QStringLiteral("EUR")));
    QLineEdit *price_text = pump_price_editor.findChild<QLineEdit *>();
    CHECK(price_text != nullptr);
    price_text->selectAll();
    QKeyEvent price_input(QEvent::KeyPress, Qt::Key_unknown,
                          Qt::NoModifier, QStringLiteral("0.10"));
    QApplication::sendEvent(price_text, &price_input);
    CHECK(pump_price_editor.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(pump_price_per_kwh - 0.36) < 1e-12 && price_writes == 1);

    // Global/pattern mode or a removed pump must reject stale numeric edits.
    price_text->selectAll();
    QKeyEvent stale_price_input(QEvent::KeyPress, Qt::Key_unknown,
                                Qt::NoModifier, QStringLiteral("0.20"));
    QApplication::sendEvent(price_text, &stale_price_input);
    constant_price_mode = false;
    CHECK(pump_price_editor.commitEdit() == NumericPropertyBinding::Result::Rejected);
    pump_price_editor.cancelEdit();
    // Global/pattern sources retain a read-only presentation of the underlying
    // stored price; the inspector disables its editor for non-constant modes.
    CHECK(pump_price_editor.refreshUnits(context, QStringLiteral("EUR")));
    CHECK(std::abs(pump_price_per_kwh - 0.36) < 1e-12 && price_writes == 1);
    constant_price_mode = true;
    CHECK(pump_price_editor.refreshUnits(context, QStringLiteral("EUR")));
    price_text->selectAll();
    QApplication::sendEvent(price_text, &stale_price_input);
    has_pump = false;
    CHECK(pump_price_editor.commitEdit() == NumericPropertyBinding::Result::Unavailable);
    pump_price_editor.cancelEdit();
    CHECK(price_writes == 1);

    // Pump simple controls use independent canonical quantities for junction
    // pressure, tank level, elapsed seconds, and speed ratio. Merely rebuilding
    // a row or changing its unit never rounds its authoritative value.
    UnitPresentationContext control_context;
    control_context.initialize(QJsonObject{
        {QStringLiteral("pressure_head"), QStringLiteral("m")},
        {QStringLiteral("water_level"), QStringLiteral("m")},
        {QStringLiteral("elapsed_time"), QStringLiteral("h")},
        {QStringLiteral("pump_speed_ratio"), QStringLiteral("1")}});
    double control_pressure_m = 2.1234567890123;
    double control_level_m = 4.9876543210987;
    double control_speed = 1.2345678901234;
    double original_control_pressure = control_pressure_m;
    double original_control_level = control_level_m;
    double original_control_speed = control_speed;
    quint64 control_elapsed_s = 12345;
    int control_writes = 0;
    bool control_node_unchanged = true;
    bool control_is_timer = true;
    bool control_set_speed = true;
    UnitNumericSpinBox control_pressure_editor(
        [&]() -> std::optional<double> {
            return control_node_unchanged ? std::optional<double>(control_pressure_m) : std::nullopt;
        },
        [&](double value) {
            if (!control_node_unchanged) return false;
            control_pressure_m = value;
            ++control_writes;
            return true;
        }, "pressure_head", "m", -10000.0, 10000.0, 0.1, 3);
    UnitNumericSpinBox control_level_editor(
        [&] { return std::optional<double>(control_level_m); },
        [&](double value) { control_level_m = value; ++control_writes; return true; },
        "water_level", "m", -10000.0, 10000.0, 0.1, 3);
    UnitNumericSpinBox control_timer_editor(
        [&]() -> std::optional<double> {
            return control_is_timer ? std::optional<double>(static_cast<double>(control_elapsed_s))
                                    : std::nullopt;
        },
        [&](double seconds) {
            if (!control_is_timer || seconds < 0.0 || seconds > 596523.0 * 3600.0)
                return false;
            control_elapsed_s = static_cast<quint64>(std::llround(seconds));
            ++control_writes;
            return true;
        }, "elapsed_time", "s", 0.0, 596523.0 * 3600.0, 900.0, 3);
    UnitNumericSpinBox control_speed_editor(
        [&]() -> std::optional<double> {
            return control_set_speed ? std::optional<double>(control_speed) : std::nullopt;
        },
        [&](double value) {
            if (!control_set_speed) return false;
            control_speed = value;
            ++control_writes;
            return true;
        }, "pump_speed_ratio", "1", 0.0, 1000.0, 0.05, 3, nullptr,
        [](const QString &) { return QStringLiteral(" ×"); });
    CHECK(control_pressure_editor.refreshUnits(control_context));
    CHECK(control_level_editor.refreshUnits(control_context));
    CHECK(control_timer_editor.refreshUnits(control_context));
    CHECK(control_speed_editor.refreshUnits(control_context));
    CHECK(control_timer_editor.suffix() == NumericText::suffix(QStringLiteral("h")));
    CHECK(std::abs(control_timer_editor.value() - 12345.0 / 3600.0) < 0.00051);
    control_context.update(QJsonObject{
        {QStringLiteral("pressure_head"), QStringLiteral("ft")},
        {QStringLiteral("water_level"), QStringLiteral("ft")},
        {QStringLiteral("elapsed_time"), QStringLiteral("min")},
        {QStringLiteral("pump_speed_ratio"), QStringLiteral("1")}});
    CHECK(control_pressure_editor.refreshUnits(control_context));
    CHECK(control_level_editor.refreshUnits(control_context));
    CHECK(control_timer_editor.refreshUnits(control_context));
    CHECK(control_speed_editor.refreshUnits(control_context));
    CHECK(control_pressure_editor.suffix() == NumericText::suffix(QStringLiteral("ft")));
    CHECK(control_level_editor.suffix() == NumericText::suffix(QStringLiteral("ft")));
    CHECK(std::abs(control_timer_editor.value() - 12345.0 / 60.0) < 0.00051);
    CHECK(control_pressure_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(control_level_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(control_timer_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(control_speed_editor.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(control_pressure_m == original_control_pressure);
    CHECK(control_level_m == original_control_level);
    CHECK(control_speed == original_control_speed);
    CHECK(control_elapsed_s == 12345 && control_writes == 0);

    QLineEdit *control_text = control_timer_editor.findChild<QLineEdit *>();
    CHECK(control_text != nullptr);
    control_text->selectAll();
    QKeyEvent control_timer_input(QEvent::KeyPress, Qt::Key_unknown,
                                  Qt::NoModifier, QStringLiteral("10"));
    QApplication::sendEvent(control_text, &control_timer_input);
    CHECK(control_timer_editor.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(control_elapsed_s == 600 && control_writes == 1);

    // Changing from timer to level before commit invalidates that editor,
    // even if its original displayed number was rounded.
    control_text->selectAll();
    QKeyEvent stale_timer_input(QEvent::KeyPress, Qt::Key_unknown,
                                Qt::NoModifier, QStringLiteral("20"));
    QApplication::sendEvent(control_text, &stale_timer_input);
    control_is_timer = false;
    CHECK(control_timer_editor.commitEdit() == NumericPropertyBinding::Result::Unavailable);
    control_timer_editor.cancelEdit();
    CHECK(control_elapsed_s == 600 && control_writes == 1);

    // Moving the trigger to another node or changing its class must reject
    // an edit interpreted under the old pressure/level meaning.
    control_text = control_pressure_editor.findChild<QLineEdit *>();
    CHECK(control_text != nullptr);
    control_text->selectAll();
    QKeyEvent control_pressure_input(QEvent::KeyPress, Qt::Key_unknown,
                                     Qt::NoModifier, QStringLiteral("20"));
    QApplication::sendEvent(control_text, &control_pressure_input);
    control_node_unchanged = false;
    CHECK(control_pressure_editor.commitEdit() == NumericPropertyBinding::Result::Unavailable);
    control_pressure_editor.cancelEdit();
    CHECK(control_pressure_m == original_control_pressure && control_writes == 1);

    control_text = control_speed_editor.findChild<QLineEdit *>();
    CHECK(control_text != nullptr);
    control_text->selectAll();
    QKeyEvent stale_speed_input(QEvent::KeyPress, Qt::Key_unknown,
                                Qt::NoModifier, QStringLiteral("1.5"));
    QApplication::sendEvent(control_text, &stale_speed_input);
    control_set_speed = false;
    CHECK(control_speed_editor.commitEdit() == NumericPropertyBinding::Result::Unavailable);
    control_speed_editor.cancelEdit();
    CHECK(control_speed == original_control_speed && control_writes == 1);
    control_set_speed = true;
    CHECK(control_speed_editor.refreshUnits(control_context));
    control_text->selectAll();
    QApplication::sendEvent(control_text, &stale_speed_input);
    CHECK(control_speed_editor.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(control_speed == 1.5 && control_writes == 2);

    // Node-quality editors (Junction, Reservoir, Tank) must preserve the
    // canonical Model value through unit changes and rounded representations.
    UnitPresentationContext quality_context;
    quality_context.initialize(QJsonObject{
        {QStringLiteral("chemical_mass_concentration"), QStringLiteral("ug/L")},
        {QStringLiteral("chemical_mass_flow_rate"), QStringLiteral("g/h")},
        {QStringLiteral("water_age"), QStringLiteral("min")}});
    double initial_concentration_mg_per_l = 12.345678901234;
    double source_concentration_mg_per_l = 0.123456789012;
    double source_mass_flow_mg_per_min = 123.4567890123;
    double initial_water_age_h = 73.123456789012;
    const double original_initial_concentration = initial_concentration_mg_per_l;
    const double original_source_concentration = source_concentration_mg_per_l;
    const double original_source_mass_flow = source_mass_flow_mg_per_min;
    const double original_initial_age = initial_water_age_h;
    int node_quality_writes = 0;
    bool quality_concentration_mode = true;
    bool quality_mass_booster_mode = false;

    UnitNumericSpinBox quality_initial_concentration(
        [&]() -> std::optional<double> { return initial_concentration_mg_per_l; },
        [&](double value) { initial_concentration_mg_per_l = value; ++node_quality_writes; return true; },
        "chemical_mass_concentration", "mg/L", 0.0, 1e9, 0.1, 6);
    UnitNumericSpinBox quality_source_concentration(
        [&]() -> std::optional<double> { return source_concentration_mg_per_l; },
        [&](double value) {
            if (!quality_concentration_mode) return false;
            source_concentration_mg_per_l = value;
            ++node_quality_writes;
            return true;
        }, "chemical_mass_concentration", "mg/L", 0.0, 1e9, 0.1, 6);
    UnitNumericSpinBox quality_source_mass_flow(
        [&]() -> std::optional<double> { return source_mass_flow_mg_per_min; },
        [&](double value) {
            if (!quality_mass_booster_mode) return false;
            source_mass_flow_mg_per_min = value;
            ++node_quality_writes;
            return true;
        }, "chemical_mass_flow_rate", "mg/min", 0.0, 1e12, 0.1, 6);
    UnitNumericSpinBox quality_water_age(
        [&]() -> std::optional<double> { return initial_water_age_h; },
        [&](double value) { initial_water_age_h = value; ++node_quality_writes; return true; },
        "water_age", "h", 0.0, 1e9, 0.1, 6);
    CHECK(quality_initial_concentration.refreshUnits(quality_context));
    CHECK(quality_source_concentration.refreshUnits(quality_context));
    CHECK(quality_source_mass_flow.refreshUnits(quality_context));
    CHECK(quality_water_age.refreshUnits(quality_context));
    CHECK(quality_initial_concentration.suffix() == NumericText::suffix(QStringLiteral("ug/L")));
    CHECK(quality_source_mass_flow.suffix() == NumericText::suffix(QStringLiteral("g/h")));
    CHECK(quality_water_age.suffix() == NumericText::suffix(QStringLiteral("min")));
    CHECK(std::abs(quality_initial_concentration.value() - original_initial_concentration * 1000.0) < 1e-6);
    CHECK(std::abs(quality_source_mass_flow.value() - original_source_mass_flow * 60.0 / 1000.0) < 1e-6);
    CHECK(std::abs(quality_water_age.value() - original_initial_age * 60.0) < 1e-6);
    CHECK(quality_initial_concentration.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    quality_context.update(QJsonObject{
        {QStringLiteral("chemical_mass_concentration"), QStringLiteral("mg/L")},
        {QStringLiteral("chemical_mass_flow_rate"), QStringLiteral("mg/min")},
        {QStringLiteral("water_age"), QStringLiteral("h")}});
    CHECK(quality_initial_concentration.refreshUnits(quality_context));
    CHECK(quality_source_concentration.refreshUnits(quality_context));
    CHECK(quality_source_mass_flow.refreshUnits(quality_context));
    CHECK(quality_water_age.refreshUnits(quality_context));
    CHECK(node_quality_writes == 0);
    CHECK(initial_concentration_mg_per_l == original_initial_concentration);
    CHECK(source_concentration_mg_per_l == original_source_concentration);
    CHECK(source_mass_flow_mg_per_min == original_source_mass_flow);
    CHECK(initial_water_age_h == original_initial_age);

    quality_context.update(QJsonObject{
        {QStringLiteral("chemical_mass_concentration"), QStringLiteral("ug/L")},
        {QStringLiteral("chemical_mass_flow_rate"), QStringLiteral("g/min")},
        {QStringLiteral("water_age"), QStringLiteral("h")}});
    CHECK(quality_source_concentration.refreshUnits(quality_context));
    CHECK(quality_source_mass_flow.refreshUnits(quality_context));
    QLineEdit *node_quality_text = quality_source_concentration.findChild<QLineEdit *>();
    CHECK(node_quality_text != nullptr);
    node_quality_text->selectAll();
    QKeyEvent node_quality_input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QStringLiteral("1250"));
    QApplication::sendEvent(node_quality_text, &node_quality_input);
    CHECK(quality_source_concentration.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(source_concentration_mg_per_l - 1.25) < 1e-12 && node_quality_writes == 1);

    // A pending concentration edit must not modify an inactive source after
    // switching to the mass-booster mode, even when the typed value is valid.
    node_quality_text->selectAll();
    QKeyEvent stale_quality_input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QStringLiteral("1500"));
    QApplication::sendEvent(node_quality_text, &stale_quality_input);
    quality_concentration_mode = false;
    quality_mass_booster_mode = true;
    CHECK(quality_source_concentration.commitEdit() == NumericPropertyBinding::Result::Rejected);
    quality_source_concentration.cancelEdit();
    CHECK(source_concentration_mg_per_l == 1.25 && node_quality_writes == 1);

    node_quality_text = quality_source_mass_flow.findChild<QLineEdit *>();
    CHECK(node_quality_text != nullptr);
    node_quality_text->selectAll();
    QKeyEvent mass_input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QStringLiteral("2.5"));
    QApplication::sendEvent(node_quality_text, &mass_input);
    CHECK(quality_source_mass_flow.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(std::abs(source_mass_flow_mg_per_min - 2500.0) < 1e-9 && node_quality_writes == 2);

    // Deleting a node (or otherwise invalidating the current property) during
    // editing must not reuse the stale editor to create an implicit write.
    bool quality_node_exists = true;
    UnitNumericSpinBox deleted_quality_node(
        [&]() -> std::optional<double> {
            return quality_node_exists ? std::optional<double>(initial_water_age_h) : std::nullopt;
        },
        [&](double value) {
            if (!quality_node_exists) return false;
            initial_water_age_h = value;
            ++node_quality_writes;
            return true;
        }, "water_age", "h", 0.0, 1e9, 0.1, 6);
    CHECK(deleted_quality_node.refreshUnits(quality_context));
    node_quality_text = deleted_quality_node.findChild<QLineEdit *>();
    CHECK(node_quality_text != nullptr);
    node_quality_text->selectAll();
    QKeyEvent deleted_quality_input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QStringLiteral("5"));
    QApplication::sendEvent(node_quality_text, &deleted_quality_input);
    quality_node_exists = false;
    CHECK(deleted_quality_node.commitEdit() == NumericPropertyBinding::Result::Unavailable);
    deleted_quality_node.cancelEdit();
    CHECK(initial_water_age_h == original_initial_age && node_quality_writes == 2);

    return 0;
}
