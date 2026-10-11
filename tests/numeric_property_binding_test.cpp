#include "config/numeric_property_binding.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "Failed line " << __LINE__ << '\n'; std::abort(); } } while (false)

int main()
{
    double model = 1.23456789012345;
    const double original = model;
    int writes = 0;
    bool available = true;
    bool accept = true;
    NumericPropertyBinding binding(
        [&]() -> std::optional<double> { return available ? std::optional<double>(model) : std::nullopt; },
        [&](double value) { ++writes; if (!accept) return false; model = value; return true; },
        [](double value, const std::string &unit) -> std::optional<double> {
            if (unit == "m") return value;
            if (unit == "ft") return value / 0.3048;
            return std::nullopt;
        },
        [](double value, const std::string &unit) -> std::optional<double> {
            if (unit == "m") return value;
            if (unit == "ft") return value * 0.3048;
            return std::nullopt;
        }, [](double value) { return value >= 0.0; });
    using Result = NumericPropertyBinding::Result;
    for (int i = 0; i < 1000; ++i) {
        CHECK(binding.presentation(i % 2 ? "m" : "ft"));
        CHECK(binding.beginEdit("ft", 4.05));
        CHECK(binding.commit() == Result::Unchanged);
    }
    CHECK(writes == 0 && std::memcmp(&model, &original, sizeof(double)) == 0);
    CHECK(binding.beginEdit("ft", 4.05));
    CHECK(binding.userEdited(4.1));
    CHECK(binding.userEdited(4.05)); // Returning to rounded baseline preserves exact model.
    CHECK(binding.commit() == Result::Unchanged && writes == 0);
    CHECK(binding.beginEdit("ft", 4.05));
    CHECK(binding.userEdited(5.0));
    CHECK(binding.presentation("m")); // Profile change/refresh doesn't reinterpret pending input.
    CHECK(binding.editUnit() == "ft");
    CHECK(binding.commit() == Result::Committed);
    CHECK(model == 5.0 * 0.3048 && writes == 1);
    CHECK(binding.beginEdit("m", model));
    binding.invalidInput();
    CHECK(binding.commit() == Result::Invalid && writes == 1);
    CHECK(!binding.userEdited(-1.0));
    CHECK(binding.commit() == Result::Invalid);
    CHECK(!binding.userEdited(std::numeric_limits<double>::infinity()));
    CHECK(binding.userEdited(2.0));
    model = 3.0;
    CHECK(binding.commit() == Result::Conflict && writes == 1);
    available = false;
    CHECK(binding.commit() == Result::Unavailable && writes == 1);
    binding.cancel();
    CHECK(!binding.beginEdit("m", 0.0));
    available = true;
    CHECK(!binding.beginEdit("unknown", 0.0));
    CHECK(binding.beginEdit("m", model));
    CHECK(binding.userEdited(2.0));
    accept = false;
    CHECK(binding.commit() == Result::Rejected && model == 3.0);
    accept = true;
    CHECK(binding.commit() == Result::Committed && model == 2.0);
    CHECK(!binding.userEdited(4.0));
    std::cout << "Numeric property binding checks passed\n";
}
