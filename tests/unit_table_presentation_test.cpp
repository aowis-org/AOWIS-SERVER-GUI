#include "config/unit_table_presentation.h"
#include "entity_inspector/map_legend_unit_presentation.h"
#include "map/core/map_symbology_length_units.h"
#include "simulation/simulation_statistics_unit_presentation.h"
#include "simulation/simulation_inspector_unit_presentation.h"

#include <QCoreApplication>
#include <QJsonObject>

#include <cmath>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace
{
void require(bool condition)
{
    if (!condition) throw std::runtime_error("unit table presentation regression");
}

void near(double actual, double expected)
{
    require(std::isfinite(actual));
    require(std::abs(actual - expected) <= 1.0e-9 * (1.0 + std::abs(expected)));
}
}

namespace
{
// Step 11c: a cross-section of actual simulation-result table headings.
// All source values are canonical. Display conversion must be reversible and
// may neither change the source nor mistake two physically different units.
void testSimulationResultColumnUnits()
{
    UnitPresentationContext context;
    QJsonObject profile;
    profile.insert(QStringLiteral("elevation"), QStringLiteral("ft"));
    profile.insert(QStringLiteral("hydraulic_head"), QStringLiteral("ft"));
    profile.insert(QStringLiteral("pressure_head"), QStringLiteral("ft"));
    profile.insert(QStringLiteral("head_gain"), QStringLiteral("ft"));
    profile.insert(QStringLiteral("head_loss"), QStringLiteral("ft"));
    profile.insert(QStringLiteral("water_level"), QStringLiteral("ft"));
    profile.insert(QStringLiteral("head_loss_gradient"), QStringLiteral("ft/1000ft"));
    profile.insert(QStringLiteral("volumetric_flow_rate"), QStringLiteral("L/s"));
    profile.insert(QStringLiteral("velocity"), QStringLiteral("ft/s"));
    profile.insert(QStringLiteral("volume"), QStringLiteral("ft3"));
    profile.insert(QStringLiteral("tank_diameter"), QStringLiteral("ft"));
    profile.insert(QStringLiteral("link_diameter"), QStringLiteral("in"));
    profile.insert(QStringLiteral("darcy_weisbach_roughness_height"), QStringLiteral("in"));
    profile.insert(QStringLiteral("chemical_mass_concentration"), QStringLiteral("ug/L"));
    profile.insert(QStringLiteral("chemical_mass_flow_rate"), QStringLiteral("g/h"));
    profile.insert(QStringLiteral("water_age"), QStringLiteral("min"));
    profile.insert(QStringLiteral("source_trace_percentage"), QStringLiteral("1"));
    profile.insert(QStringLiteral("efficiency"), QStringLiteral("1"));
    profile.insert(QStringLiteral("operating_time_percentage"), QStringLiteral("1"));
    profile.insert(QStringLiteral("valve_position"), QStringLiteral("1"));
    profile.insert(QStringLiteral("power"), QStringLiteral("hp"));
    profile.insert(QStringLiteral("energy_intensity"), QStringLiteral("kW.h/ft3"));
    context.initialize(profile);

    struct SimulationColumnCase
    {
        QString title;
        std::string_view quantity;
        double canonical;
        double expected_display;
    };
    // All headings below are present in the live node/link/result tables.
    const SimulationColumnCase cases[] = {
        {QStringLiteral("Elevation Direct [m]"), "elevation", 0.3048, 1.0},
        {QStringLiteral("Head [m]"), "hydraulic_head", 0.3048, 1.0},
        {QStringLiteral("Head Pressure [m]"), "pressure_head", 0.3048, 1.0},
        {QStringLiteral("Head Gain [m]"), "head_gain", 0.3048, 1.0},
        {QStringLiteral("Head Loss [m]"), "head_loss", 0.3048, 1.0},
        {QStringLiteral("Level Water [m]"), "water_level", 0.3048, 1.0},
        {QStringLiteral("Head Loss Unit [m/km]"), "head_loss_gradient", 2.5, 2.5},
        {QStringLiteral("Demand Delivered [m³/h]"), "volumetric_flow_rate", 3.6, 1.0},
        {QStringLiteral("Flow Leakage [m³/h]"), "volumetric_flow_rate", 3.6, 1.0},
        {QStringLiteral("Velocity [m/s]"), "velocity", 0.3048, 1.0},
        {QStringLiteral("Volume Mixing-Zone [m³]"), "volume", 0.028316846592, 1.0},
        {QStringLiteral("Diameter [m]"), "tank_diameter", 0.3048, 1.0},
        {QStringLiteral("Diameter [mm]"), "link_diameter", 25.4, 1.0},
        {QStringLiteral("Roughness DW Effective [mm]"), "darcy_weisbach_roughness_height", 25.4, 1.0},
        {QStringLiteral("Chemical Concentration [mg/L]"), "chemical_mass_concentration", 0.25, 250.0},
        {QStringLiteral("Initial Water Age [h]"), "water_age", 1.5, 90.0},
        {QStringLiteral("Quality Source Mass Flow [mg/min]"), "chemical_mass_flow_rate", 1000.0, 60.0},
        {QStringLiteral("Source Trace [%]"), "source_trace_percentage", 25.0, 0.25},
        {QStringLiteral("Efficiency [%]"), "efficiency", 75.0, 0.75},
        {QStringLiteral("Time Online [%]"), "operating_time_percentage", 20.0, 0.20},
        {QStringLiteral("Power [kW]"), "power", 0.7456998715822702, 1.0},
        {QStringLiteral("Energy Intensity Average [kWh/m³]"), "energy_intensity", 1.0, 0.028316846592}
    };
    for (const SimulationColumnCase &test : cases)
    {
        require(UnitTablePresentation::quantityForTitle(test.title) == test.quantity);
        const std::optional<double> displayed = UnitTablePresentation::toDisplayForTitle(
            context, test.title, test.canonical);
        require(displayed.has_value());
        near(*displayed, test.expected_display);
        const std::optional<double> restored = UnitTablePresentation::toCanonicalForTitle(
            context, test.title, *displayed);
        require(restored.has_value());
        near(*restored, test.canonical);
        const QString formatted = UnitTablePresentation::formattedTitle(test.title, context);
        require(formatted != test.title);
        require(formatted.endsWith(QStringLiteral("[")
            + UnitTablePresentation::unitSymbol(
                UnitTablePresentation::selectedUnit(context, test.quantity))
            + QStringLiteral("]")));
    }

    // A valve's pressure, flow and position settings share the same column.
    // Their quantities must remain row-specific rather than being inferred
    // from the column title (which deliberately has no physical unit).
    require(UnitTablePresentation::quantityForTitle(QStringLiteral("Setting")).empty());
    near(*UnitTablePresentation::toDisplay(context, "pressure_head", 0.3048), 1.0);
    near(*UnitTablePresentation::toDisplay(context, "volumetric_flow_rate", 3.6), 1.0);
    near(*UnitTablePresentation::toDisplay(context, "valve_position", 50.0), 0.5);
    require(UnitTablePresentation::toDisplayForTitle(
        context, QStringLiteral("Setting"), 50.0).value_or(-1.0) == 50.0);

    // The solver's native EPANET statistics and diagnostic text are not unit
    // profile values. Percentage-like scalar quantities stay distinct.
    require(UnitTablePresentation::quantityForTitle(QStringLiteral("Friction Factor")).empty());
    require(UnitTablePresentation::quantityForTitle(QStringLiteral("Status")).empty());
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    UnitPresentationContext presentation;
    testSimulationResultColumnUnits();
    require(UnitTablePresentation::quantityForTitle(QStringLiteral("Demand Base [m³/h]"))
            == std::string_view("base_demand_flow_rate"));
    require(UnitTablePresentation::quantityForTitle(QStringLiteral("Customer Demand Base [m³/h]"))
            == std::string_view("customer_demand_flow_rate"));
    require(UnitTablePresentation::quantityForTitle(QStringLiteral("Head Pressure [m]"))
            == std::string_view("pressure_head"));
    require(UnitTablePresentation::quantityForTitle(QStringLiteral("Length Effective [m]"))
            == std::string_view("length"));
    require(UnitTablePresentation::quantityForTitle(QStringLiteral("Roughness DW [mm]"))
            == std::string_view("darcy_weisbach_roughness_height"));
    require(UnitTablePresentation::quantityForTitle(QStringLiteral("Valve Type")).empty());
    require(UnitTablePresentation::isUnitDependentTextColumn(QStringLiteral("Demand Details")));
    require(!UnitTablePresentation::isUnitDependentTextColumn(QStringLiteral("Description")));
    require(!UnitTablePresentation::isUnitDependentTextColumn(QStringLiteral("Demand Base [m³/h]")));
    // Ensure tiny nonzero numbers are not rounded all the way to displayed zero.
    require(UnitTablePresentation::formatNumber(0.0, 3, QStringLiteral(" m")) == QStringLiteral("0 m"));
    require(UnitTablePresentation::formatNumber(1.0e-12, 3, QStringLiteral(" m")) == QStringLiteral("1e-12 m"));
    require(UnitTablePresentation::formatNumber(-1.0e-12, 3, QStringLiteral(" m")) == QStringLiteral("-1e-12 m"));
    require(UnitTablePresentation::formatNumber(0.00049, 3, QStringLiteral(" m")) == QStringLiteral("0.00049 m"));
    // Position is an attachment geometry percentage; do not conflate it with
    // the valve-position quantity.
    require(UnitTablePresentation::quantityForTitle(QStringLiteral("Pipe Position [%]")).empty());

    QJsonObject units;
    units.insert(QStringLiteral("volumetric_flow_rate"), QStringLiteral("L/s"));
    units.insert(QStringLiteral("length"), QStringLiteral("ft"));
    units.insert(QStringLiteral("pressure_head"), QStringLiteral("ft"));
    units.insert(QStringLiteral("link_diameter"), QStringLiteral("in"));
    units.insert(QStringLiteral("chemical_mass_concentration"), QStringLiteral("ug/L"));
    presentation.update(units);

    // Simulation inspectors format canonical results without changing stored
    // doubles, even when the chosen unit makes a value very small.
    {
        const double canonical_head = 1.234567890123;
        require(SimulationInspectorUnitPresentation::format(
                    presentation, "pressure_head", canonical_head, 3)
                == QStringLiteral("4.05 ft"));
        require(SimulationInspectorUnitPresentation::format(
                    presentation, "chemical_mass_concentration", 0.125, 3)
                == QStringLiteral("125 µg/L"));
        require(SimulationInspectorUnitPresentation::format(
                    presentation, "volumetric_flow_rate", 1.0e-12, 3)
                != QStringLiteral("0 m³/h"));
        // Valve settings must not conflate pressure head, flow and position.
        require(SimulationInspectorUnitPresentation::format(
                    presentation, "volumetric_flow_rate", 3.6, 3)
                == QStringLiteral("1 L/s"));
        QJsonObject valve_units = units;
        valve_units.insert(QStringLiteral("valve_position"), QStringLiteral("1"));
        require(presentation.update(valve_units));
        require(SimulationInspectorUnitPresentation::format(
                    presentation, "valve_position", 50.0, 3)
                == QStringLiteral("0.5"));
        require(presentation.update(units));
        require(canonical_head == 1.234567890123);
        QJsonObject simulation_units = units;
        simulation_units.insert(QStringLiteral("water_age"), QStringLiteral("min"));
        simulation_units.insert(QStringLiteral("chemical_mass_flow_rate"), QStringLiteral("g/h"));
        require(presentation.update(simulation_units));
        require(SimulationInspectorUnitPresentation::format(
                    presentation, "water_age", 1.5, 3)
                == QStringLiteral("90 min"));
        require(SimulationInspectorUnitPresentation::format(
                    presentation, "chemical_mass_flow_rate", 1000.0, 3)
                == QStringLiteral("60 g/h"));
        require(presentation.update(units));
    }

    // Physical icon-size controls retain integer-metre slider positions.
    // Only tooltip representation follows the length profile.
    {
        const MapSymbologyLengthUnits::Presentation feet =
            MapSymbologyLengthUnits::forContext(presentation);
        require(feet.unit == QStringLiteral("ft"));
        near(150.0 * feet.metres_to_display, 492.1259842519685);
        require(feet.decimals >= 1);
        const double stored_size_m = 150.0;
        QJsonObject alternate_units = units;
        alternate_units.insert(QStringLiteral("length"), QStringLiteral("mi"));
        require(presentation.update(alternate_units));
        const MapSymbologyLengthUnits::Presentation miles =
            MapSymbologyLengthUnits::forContext(presentation);
        require(miles.unit == QStringLiteral("mi"));
        near(150.0 * miles.metres_to_display, 150.0 / 1609.344);
        require(miles.decimals >= 4);
        require(stored_size_m == 150.0);
        require(presentation.update(units));
    }

    // The simulation statistics dialog stores canonical head/flow errors.
    // Changing the profile affects labels only, never the original doubles.
    {
        UnitPresentationContext simulation_units;
        QJsonObject simulation_profile;
        simulation_profile.insert(QStringLiteral("hydraulic_head"), QStringLiteral("ft"));
        simulation_profile.insert(QStringLiteral("volumetric_flow_rate"), QStringLiteral("L/s"));
        simulation_units.initialize(simulation_profile);
        const double original_head_m = 3.123456789;
        const double original_flow_m3_per_h = 3.6;
        require(SimulationStatisticsUnits::selectedUnit(simulation_units, true) == QStringLiteral("ft"));
        require(SimulationStatisticsUnits::selectedUnit(simulation_units, false) == QStringLiteral("L/s"));
        near(*UnitTablePresentation::toDisplay(simulation_units, "hydraulic_head", original_head_m),
             original_head_m / 0.3048);
        near(*UnitTablePresentation::toDisplay(simulation_units, "volumetric_flow_rate", original_flow_m3_per_h), 1.0);
        require(SimulationStatisticsUnits::formatWithUnit(simulation_units, false, original_flow_m3_per_h)
                == QStringLiteral("1 L/s"));
        require(SimulationStatisticsUnits::formatWithUnit(simulation_units, true, 0.0)
                == QStringLiteral("0 ft"));
        require(original_head_m == 3.123456789);
        require(original_flow_m3_per_h == 3.6);
    }

    // Map legends convert visible tick ranges only. The color ramp retains
    // the canonical values used by network symbology and simulation.
    using MapRange = MapLegendUnitPresentation::Range;
    const double canonical_base_demand = 3.6;
    const MapRange demand_legend = MapLegendUnitPresentation::range(
        presentation, QStringLiteral("Base Demand"), 0.0, canonical_base_demand);
    near(demand_legend.maximum, 1.0);
    require(demand_legend.unit == QStringLiteral("L/s"));
    require(canonical_base_demand == 3.6);
    const MapRange diameter_legend = MapLegendUnitPresentation::range(
        presentation, QStringLiteral("Diameter"), 0.0, 25.4);
    near(diameter_legend.maximum, 1.0);
    require(diameter_legend.unit == QStringLiteral("in"));
    const MapRange chlorine_legend = MapLegendUnitPresentation::range(
        presentation, QStringLiteral("Cl₂"), 0.0, 1.0);
    near(chlorine_legend.maximum, 1000.0);
    require(chlorine_legend.unit == QStringLiteral("µg/L"));
    const MapRange no_unit_legend = MapLegendUnitPresentation::range(
        presentation, QStringLiteral("Hazen-Williams C"), 10.0, 120.0);
    near(no_unit_legend.minimum, 10.0);
    near(no_unit_legend.maximum, 120.0);
    require(no_unit_legend.unit.isEmpty());

    QJsonObject map_units = units;
    map_units.insert(QStringLiteral("elevation"), QStringLiteral("ft"));
    map_units.insert(QStringLiteral("pressure_head"), QStringLiteral("ft"));
    map_units.insert(QStringLiteral("water_age"), QStringLiteral("min"));
    map_units.insert(QStringLiteral("darcy_weisbach_roughness_height"), QStringLiteral("in"));
    require(presentation.update(map_units));
    const MapRange terrain_legend = MapLegendUnitPresentation::range(
        presentation, QStringLiteral("Elevation"), 0.0, 100.0);
    near(terrain_legend.maximum, 328.0839895013123);
    require(terrain_legend.unit == QStringLiteral("ft"));
    const MapRange head_legend = MapLegendUnitPresentation::range(
        presentation, QStringLiteral("Pressure Head"), 0.0, 1.0);
    near(head_legend.maximum, 3.280839895013123);
    const MapRange age_legend = MapLegendUnitPresentation::range(
        presentation, QStringLiteral("Water Age"), 0.0, 1.0);
    near(age_legend.maximum, 60.0);
    require(age_legend.unit == QStringLiteral("min"));
    const MapRange roughness_legend = MapLegendUnitPresentation::range(
        presentation, QStringLiteral("Absolute Roughness ε"), 0.0, 25.4);
    near(roughness_legend.maximum, 1.0);
    require(roughness_legend.unit == QStringLiteral("in"));
    // Length is already set to feet; verify large ranges use miles without
    // forcing a redundant presentation-generation change.
    const MapRange long_length = MapLegendUnitPresentation::range(
        presentation, QStringLiteral("Length"), 0.0, 2000.0);
    require(long_length.unit == QStringLiteral("mi"));
    near(long_length.maximum, 2000.0 / 1609.344);
    // Restore the profile so existing table assertions stay independent.
    require(presentation.update(units));

    require(UnitTablePresentation::formattedTitle(QStringLiteral("Demand Base [m³/h]"), presentation)
            == QStringLiteral("Demand Base [L/s]"));
    require(UnitTablePresentation::formattedTitle(QStringLiteral("Quality Initial [mg/L]"), presentation)
            == QStringLiteral("Quality Initial [µg/L]"));
    require(UnitTablePresentation::formattedTitle(QStringLiteral("Valve Type"), presentation)
            == QStringLiteral("Valve Type"));

    const double canonical_flow = 1.234567890123;
    const auto flow = UnitTablePresentation::toDisplay(presentation, "volumetric_flow_rate", canonical_flow);
    require(flow.has_value());
    near(*flow, canonical_flow * 1000.0 / 3600.0);
    const auto restored = UnitTablePresentation::toCanonical(presentation, "volumetric_flow_rate", *flow);
    require(restored.has_value());
    near(*restored, canonical_flow);
    // Formatting never writes to, rounds, or mutates the canonical number.
    require(canonical_flow == 1.234567890123);

    near(*UnitTablePresentation::toDisplay(presentation, "length", 1.0), 3.280839895013123);
    near(*UnitTablePresentation::toDisplay(presentation, "pressure_head", 1.0), 3.280839895013123);
    near(*UnitTablePresentation::toDisplay(presentation, "link_diameter", 25.4), 1.0);
    near(*UnitTablePresentation::toDisplay(presentation, "chemical_mass_concentration", 1.0), 1000.0);

    // Compound demand text must change units on demand without changing storage.
    const double category_rate = 3.123456789;
    require(UnitTablePresentation::formatDemandRate(presentation, category_rate)
            == QStringLiteral("0.868 L/s"));
    require(category_rate == 3.123456789);
    require(UnitTablePresentation::formatDemandRate(presentation, 1.0e-12)
            != QStringLiteral("0 L/s"));

    // Energy prices are currency/energy, so conversion is inverse-energy.
    const QString price_title = QStringLiteral("Energy Price [XAF/kWh]");
    require(UnitTablePresentation::isEnergyPriceTitle(price_title));
    require(!UnitTablePresentation::isEnergyPriceTitle(QStringLiteral("Cost Average [XAF/day]")));
    units.insert(QStringLiteral("energy"), QStringLiteral("MJ"));
    require(presentation.update(units));
    require(UnitTablePresentation::formattedTitle(price_title, presentation)
            == QStringLiteral("Energy Price [XAF/MJ]"));
    near(*UnitTablePresentation::toDisplayForTitle(presentation, price_title, 0.18), 0.05);
    near(*UnitTablePresentation::toCanonicalForTitle(presentation, price_title, 0.05), 0.18);

    units.insert(QStringLiteral("energy"), QStringLiteral("J"));
    require(presentation.update(units));
    require(UnitTablePresentation::formattedTitle(price_title, presentation)
            == QStringLiteral("Energy Price [XAF/J]"));
    const QString per_joule = UnitTablePresentation::formatEnergyPrice(presentation, 0.18);
    require(per_joule != QStringLiteral("0") && per_joule != QStringLiteral("—"));
    near(*UnitTablePresentation::toCanonicalForTitle(presentation, price_title,
         *UnitTablePresentation::toDisplayForTitle(presentation, price_title, 0.18)), 0.18);
    require(UnitTablePresentation::formatEnergyPrice(presentation, 0.0) == QStringLiteral("0"));
    require(!UnitTablePresentation::toDisplayForTitle(presentation, price_title, -1.0).has_value());
    // Control geometry and filtering must retain their original canonical values.
    require(category_rate == 3.123456789);

    // Reaction coefficients retain their canonical value and display
    // order-dependent dimensions instead of a misleading column-wide suffix.
    units.insert(QStringLiteral("first_order_bulk_reaction_coefficient"), QStringLiteral("/h"));
    units.insert(QStringLiteral("first_order_wall_reaction_coefficient"), QStringLiteral("ft/d"));
    units.insert(QStringLiteral("zero_order_wall_reaction_coefficient"), QStringLiteral("mg/m2/h"));
    require(presentation.update(units));
    using ReactionKind = UnitTablePresentation::ReactionKind;
    near(*UnitTablePresentation::reactionValue(presentation, ReactionKind::Bulk, 1.0, 24.0), 1.0);
    near(*UnitTablePresentation::reactionValue(presentation, ReactionKind::Bulk, 0.0, 24.0), 1.0);
    near(*UnitTablePresentation::reactionValue(presentation, ReactionKind::Bulk, -1.0, 24.0), 1.0);
    require(UnitTablePresentation::reactionUnit(presentation, ReactionKind::Bulk, 1.0)
            == QStringLiteral("/h"));
    require(UnitTablePresentation::reactionUnit(presentation, ReactionKind::Bulk, 0.0)
            == QStringLiteral("(mg/L)/h"));
    require(UnitTablePresentation::reactionUnit(presentation, ReactionKind::Bulk, 2.0)
            == QStringLiteral("(mg/L)^-1/h"));
    near(*UnitTablePresentation::reactionValue(presentation, ReactionKind::Wall, 1.0, 0.3048), 1.0);
    near(*UnitTablePresentation::reactionValue(presentation, ReactionKind::Wall, 0.0, 24.0), 1.0);
    require(UnitTablePresentation::reactionUnit(presentation, ReactionKind::Wall, 0.0)
            == QStringLiteral("mg/m²/h"));
    require(UnitTablePresentation::formatReactionCoefficient(
                presentation, ReactionKind::Wall, 1.0, 0.3048)
            == QStringLiteral("1 ft/d"));
    require(!UnitTablePresentation::reactionValue(
                presentation, ReactionKind::Wall, 2.0, 5.0).has_value());
    require(UnitTablePresentation::formatReactionCoefficient(
                presentation, ReactionKind::Wall, 2.0, 5.0) == QStringLiteral("—"));
    require(UnitTablePresentation::reactionColumnTooltip(
                presentation, ReactionKind::Bulk).contains(QStringLiteral("/h")));
    require(UnitTablePresentation::reactionCellTooltip(
                presentation, ReactionKind::Wall, 0.0).contains(QStringLiteral("mg/m²/h")));
    const double canonical_reaction = 24.123456789;
    require(UnitTablePresentation::formatReactionCoefficient(
                presentation, ReactionKind::Bulk, 1.0, canonical_reaction)
            == QStringLiteral("1.00514403 /h"));
    require(canonical_reaction == 24.123456789);
    const QString tiny_reaction = UnitTablePresentation::formatReactionCoefficient(
        presentation, ReactionKind::Bulk, 1.0, 1.0e-10);
    require(!tiny_reaction.startsWith(QStringLiteral("0 ")));
    require(!tiny_reaction.startsWith(QStringLiteral("0.00000000 ")));

    const auto generation = presentation.generation();
    require(!presentation.update(units));
    require(presentation.generation() == generation);
    units.insert(QStringLiteral("volumetric_flow_rate"), QStringLiteral("m3/h"));
    require(presentation.update(units));
    require(presentation.generation() != generation);
    near(*UnitTablePresentation::toDisplay(presentation, "volumetric_flow_rate", canonical_flow),
         canonical_flow);
    // Independent source preferences do not change canonical hydraulic values.
    units.insert(QStringLiteral("volumetric_flow_rate"), QStringLiteral("L/s"));
    units.insert(QStringLiteral("base_demand_flow_rate"), QStringLiteral("m3/h"));
    units.insert(QStringLiteral("customer_demand_flow_rate"), QStringLiteral("US gal/min"));
    units.insert(QStringLiteral("tank_initial_level"), QStringLiteral("ft"));
    units.insert(QStringLiteral("tank_minimum_level"), QStringLiteral("m"));
    units.insert(QStringLiteral("tank_maximum_level"), QStringLiteral("ft"));
    units.insert(QStringLiteral("tank_minimum_volume"), QStringLiteral("ft3"));
    require(presentation.update(units));
    require(UnitTablePresentation::formattedTitle(QStringLiteral("Demand Base [m³/h]"), presentation)
            == QStringLiteral("Demand Base [m³/h]"));
    require(UnitTablePresentation::formattedTitle(
                QStringLiteral("Customer Demand Base [m³/h]"), presentation)
            == QStringLiteral("Customer Demand Base [US gal/min]"));
    require(UnitTablePresentation::formatDemandRate(presentation, 3.6)
            == QStringLiteral("3.6 m³/h"));
    require(UnitTablePresentation::formatDemandRate(
                presentation, 3.6, 3, "customer_demand_flow_rate")
            .endsWith(QStringLiteral(" US gal/min")));
    near(*UnitTablePresentation::toDisplayForTitle(
             presentation, QStringLiteral("Customer Demand Base [m³/h]"), 3.6),
         3.6 / aowis::units::usGallonsPerMinuteToCubicMetresPerHour(1.0));
    near(*UnitTablePresentation::toCanonicalForTitle(
             presentation, QStringLiteral("Customer Demand Base [m³/h]"),
             *UnitTablePresentation::toDisplayForTitle(
                 presentation, QStringLiteral("Customer Demand Base [m³/h]"), canonical_flow)),
         canonical_flow);
    require(UnitTablePresentation::formattedTitle(QStringLiteral("Level Initial [m]"), presentation)
            == QStringLiteral("Level Initial [ft]"));
    require(UnitTablePresentation::formattedTitle(QStringLiteral("Level Minimum [m]"), presentation)
            == QStringLiteral("Level Minimum [m]"));
    require(UnitTablePresentation::formattedTitle(QStringLiteral("Volume Minimum [m³]"), presentation)
            == QStringLiteral("Volume Minimum [ft3]"));
    near(*UnitTablePresentation::toDisplayForTitle(
             presentation, QStringLiteral("Volume Minimum [m³]"), 1.0),
         1.0 / aowis::units::cubic_metres_per_cubic_foot);
    near(MapLegendUnitPresentation::range(
             presentation, QStringLiteral("Base Demand"), 0.0, 3.6).maximum, 3.6);
    require(canonical_flow == 1.234567890123);
    return 0;
}
