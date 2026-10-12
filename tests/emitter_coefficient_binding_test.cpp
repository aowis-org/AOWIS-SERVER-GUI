#include "entity_inspector/emitter_coefficient_binding.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#define CHECK(condition) do { \
    if (!(condition)) { std::fprintf(stderr, "FAIL: %s at %s:%d\n", #condition, __FILE__, __LINE__); std::exit(1); } \
} while (false)

static void expectNear(double actual, double expected, double tolerance = 1e-12)
{
    CHECK(std::isfinite(actual));
    CHECK(std::abs(actual - expected) <= tolerance * std::max(1.0, std::abs(expected)));
}

int main()
{
    using EmitterCoefficientUnits::Unit;
    const Unit canonical{"m3/h", "m", 0.5};
    const Unit imperial{"US gal/min", "ft", 0.5};
    const Unit pressure_imperial{"US gal/min", "psi", 0.5, 1.2};
    const std::optional<std::string> key = EmitterCoefficientUnits::encode(imperial);
    CHECK(key.has_value());
    const std::optional<Unit> decoded = EmitterCoefficientUnits::decode(*key);
    CHECK(decoded && decoded->flow == imperial.flow && decoded->head == imperial.head &&
          decoded->exponent == imperial.exponent);
    CHECK(!EmitterCoefficientUnits::decode("US gal/min|ft|garbage"));
    CHECK(!EmitterCoefficientUnits::decode("US gal/min|ft|0"));
    CHECK(!EmitterCoefficientUnits::decode("US gal/min|ft|0.5|other"));
    const std::optional<std::string> pressure_key = EmitterCoefficientUnits::encode(pressure_imperial);
    CHECK(pressure_key);
    const std::optional<Unit> pressure_decoded = EmitterCoefficientUnits::decode(*pressure_key);
    CHECK(pressure_decoded && pressure_decoded->specific_gravity == 1.2 &&
          pressure_decoded->head == "psi" && pressure_decoded->exponent == 0.5);
    const std::optional<double> pressure_display = EmitterCoefficientUnits::toDisplay(1.0, pressure_imperial);
    CHECK(pressure_display);
    const std::optional<double> pressure_head_scale = aowis::units::convert(
        "pressure_head", 1.0, "m", "psi", 1.2);
    const std::optional<double> pressure_flow_scale = aowis::units::convert(
        "volumetric_flow_rate", 1.0, "m3/h", "US gal/min");
    CHECK(pressure_head_scale && pressure_flow_scale);
    expectNear(*pressure_display, *pressure_flow_scale / std::sqrt(*pressure_head_scale));
    const std::optional<double> pressure_restored = EmitterCoefficientUnits::toCanonical(
        *pressure_display, pressure_imperial);
    CHECK(pressure_restored);
    expectNear(*pressure_restored, 1.0);
    CHECK(!EmitterCoefficientUnits::encode({"US gal/min", "psi", 0.5, 0.0}));

    CHECK(!EmitterCoefficientUnits::toDisplay(1.0, {"bad unit", "m", 0.5}));
    CHECK(!EmitterCoefficientUnits::toCanonical(1.0, {"m3/h", "bad", 0.5}));
    CHECK(!EmitterCoefficientUnits::toDisplay(1.0, {"m3/h", "ft", 1000000.0}));

    const double canonical_coefficient = 1.23456789012345;
    CHECK(EmitterCoefficientUnits::toDisplay(canonical_coefficient, canonical) == canonical_coefficient);
    const std::optional<double> converted =
        EmitterCoefficientUnits::toDisplay(canonical_coefficient, imperial);
    CHECK(converted);
    const std::optional<double> reversed = EmitterCoefficientUnits::toCanonical(*converted, imperial);
    CHECK(reversed);
    expectNear(*reversed, canonical_coefficient);
    const std::optional<double> flow_scale = aowis::units::convert(
        "volumetric_flow_rate", 1.0, "m3/h", "US gal/min");
    const std::optional<double> head_scale = aowis::units::convert(
        "pressure_head", 1.0, "m", "ft");
    CHECK(flow_scale && head_scale);
    expectNear(*converted, canonical_coefficient * *flow_scale / std::sqrt(*head_scale));

    double stored_coefficient = canonical_coefficient;
    double stored_exponent = 0.5;
    int writes = 0;
    EmitterCoefficientBindingStorage storage(
        [&]() -> std::optional<double> { return stored_coefficient; },
        [&](double value) { stored_coefficient = value; ++writes; return true; },
        [&]() -> std::optional<double> { return stored_exponent; });
    NumericPropertyBinding &binding = storage.binding();

    // Read/display and no-op edit cannot change a more precise stored value.
    CHECK(binding.presentation(*key));
    const double rounded = std::round(*binding.presentation(*key) * 100.0) / 100.0;
    CHECK(binding.beginEdit(*key, rounded));
    CHECK(binding.commit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(stored_coefficient == canonical_coefficient && writes == 0);
    CHECK(binding.beginEdit(*key, rounded));
    CHECK(binding.userEdited(rounded));
    CHECK(binding.commit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(stored_coefficient == canonical_coefficient && writes == 0);

    // A deliberate edit converts back to canonical; no unit profile stored in Model.
    CHECK(binding.beginEdit(*key, rounded));
    CHECK(binding.userEdited(rounded + 1.0));
    const std::optional<double> expected = EmitterCoefficientUnits::toCanonical(rounded + 1.0, imperial);
    CHECK(expected);
    CHECK(binding.commit() == NumericPropertyBinding::Result::Committed);
    expectNear(stored_coefficient, *expected);
    CHECK(writes == 1);

    // Exponent changes before the coefficient commit must reject the pending edit.
    const double latest = *binding.presentation(*key);
    CHECK(binding.beginEdit(*key, latest));
    CHECK(binding.userEdited(latest + 1.0));
    stored_exponent = 0.75;
    CHECK(binding.commit() == NumericPropertyBinding::Result::Rejected);
    CHECK(writes == 1);
    binding.cancel();
    // A fresh edit with the revised exponent is accepted.
    const std::optional<std::string> updated =
        EmitterCoefficientUnits::encode({"US gal/min", "ft", stored_exponent});
    CHECK(updated);
    const double fresh = *binding.presentation(*updated);
    CHECK(binding.beginEdit(*updated, fresh));
    CHECK(binding.userEdited(fresh + 0.25));
    CHECK(binding.commit() == NumericPropertyBinding::Result::Committed);
    CHECK(writes == 2);

    // External coefficient modifications are detected by the generic binding.
    const double baseline = *binding.presentation(*updated);
    CHECK(binding.beginEdit(*updated, baseline));
    CHECK(binding.userEdited(baseline + 0.5));
    stored_coefficient += 0.123;
    CHECK(binding.commit() == NumericPropertyBinding::Result::Conflict);
    CHECK(writes == 2);

    std::puts("Emitter coefficient conversion and binding checks passed");
}
