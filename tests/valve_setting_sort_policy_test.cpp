#include "network/valve_setting_sort_policy.h"
#include "unit_test_check.h"

#include <algorithm>
#include <array>
#include <optional>

namespace
{
enum class ValveType
{
    PRV, PSV, PBV, FCV, TCV, PCV, GPV
};

using ValveSettingSortPolicy::Group;
using ValveSettingSortPolicy::Key;

void testGroupClassification()
{
    CHECK(ValveSettingSortPolicy::groupForType(ValveType::PRV) == Group::PressureHead);
    CHECK(ValveSettingSortPolicy::groupForType(ValveType::PSV) == Group::PressureHead);
    CHECK(ValveSettingSortPolicy::groupForType(ValveType::PBV) == Group::PressureHead);
    CHECK(ValveSettingSortPolicy::groupForType(ValveType::FCV) == Group::FlowRate);
    CHECK(ValveSettingSortPolicy::groupForType(ValveType::TCV) == Group::LossCoefficient);
    CHECK(ValveSettingSortPolicy::groupForType(ValveType::PCV) == Group::Position);
    CHECK(ValveSettingSortPolicy::groupForType(ValveType::GPV) == Group::Curve);
}

void testDimensionalOrdering()
{
    // FCV flow (1.0) must not precede PRV pressure head (100.0) merely
    // because its numeric magnitude is smaller.
    const Key pressure{Group::PressureHead, 100.0};
    const Key flow{Group::FlowRate, 1.0};
    CHECK(ValveSettingSortPolicy::compare(pressure, flow) < 0);
    CHECK(ValveSettingSortPolicy::compare(flow, pressure) > 0);

    // Pressure-regulating valves share compatible dimensions and are sorted
    // numerically, even when they have different valve types.
    const Key lower_pressure{Group::PressureHead, 1.0};
    CHECK(ValveSettingSortPolicy::compare(lower_pressure, pressure) < 0);
    CHECK(ValveSettingSortPolicy::compare(pressure, lower_pressure) > 0);
    CHECK(ValveSettingSortPolicy::compare(pressure, pressure) == 0);

    // Each remaining family is separate, including dimensionless TCV loss
    // coefficients and PCV positions (which are fractions/percentages).
    CHECK(ValveSettingSortPolicy::compare({Group::LossCoefficient, 0.5},
                                          {Group::Position, 0.5}) < 0);
    CHECK(ValveSettingSortPolicy::compare({Group::Position, 50.0},
                                          {Group::Curve, std::nullopt}) < 0);
}

void testMissingResultsAndSortConsistency()
{
    const Key known{Group::FlowRate, 10.0};
    const Key missing{Group::FlowRate, std::nullopt};
    CHECK(ValveSettingSortPolicy::compare(known, missing) < 0);
    CHECK(ValveSettingSortPolicy::compare(missing, known) > 0);
    CHECK(ValveSettingSortPolicy::compare(missing, missing) == 0);

    std::array<Key, 7> keys{{
        {Group::Position, 90.0},
        {Group::FlowRate, 20.0},
        {Group::PressureHead, 60.0},
        {Group::Curve, std::nullopt},
        {Group::PressureHead, 5.0},
        {Group::FlowRate, std::nullopt},
        {Group::LossCoefficient, 8.0}
    }};
    std::sort(keys.begin(), keys.end(), [](const Key &first, const Key &second)
    {
        return ValveSettingSortPolicy::compare(first, second) < 0;
    });
    CHECK(keys[0].group == Group::PressureHead && *keys[0].canonical_value == 5.0);
    CHECK(keys[1].group == Group::PressureHead && *keys[1].canonical_value == 60.0);
    CHECK(keys[2].group == Group::FlowRate && *keys[2].canonical_value == 20.0);
    CHECK(keys[3].group == Group::FlowRate && !keys[3].canonical_value.has_value());
    CHECK(keys[4].group == Group::LossCoefficient);
    CHECK(keys[5].group == Group::Position);
    CHECK(keys[6].group == Group::Curve);
}
}

int main()
{
    testGroupClassification();
    testDimensionalOrdering();
    testMissingResultsAndSortConsistency();
    return 0;
}
