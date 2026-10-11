#ifndef VALVE_SETTING_SORT_POLICY_H
#define VALVE_SETTING_SORT_POLICY_H

#include <optional>

// A valve table contains incompatible setting dimensions in the same column.
// Sorting compares groups first; numerical values are comparable only inside
// the same group. A missing simulation result sorts after a present value in
// ascending order. Never compare a flow setting numerically to pressure head.
namespace ValveSettingSortPolicy
{
enum class Group : int
{
    PressureHead = 0,
    FlowRate = 1,
    LossCoefficient = 2,
    Position = 3,
    Curve = 4,
    Unknown = 5
};

template<typename ValveType>
constexpr Group groupForType(ValveType type)
{
    switch (type)
    {
    case ValveType::PRV:
    case ValveType::PSV:
    case ValveType::PBV:
        return Group::PressureHead;
    case ValveType::FCV:
        return Group::FlowRate;
    case ValveType::TCV:
        return Group::LossCoefficient;
    case ValveType::PCV:
        return Group::Position;
    case ValveType::GPV:
        return Group::Curve;
    }
    return Group::Unknown;
}

struct Key
{
    Group group = Group::Unknown;
    std::optional<double> canonical_value;
};

// -1: first sorts before second; +1: after; 0: equal (caller can tie-break).
inline int compare(const Key &first, const Key &second)
{
    if (first.group != second.group)
        return static_cast<int>(first.group) < static_cast<int>(second.group) ? -1 : 1;

    if (first.canonical_value.has_value() != second.canonical_value.has_value())
        return first.canonical_value.has_value() ? -1 : 1;

    if (first.canonical_value.has_value())
    {
        if (*first.canonical_value < *second.canonical_value)
            return -1;
        if (*second.canonical_value < *first.canonical_value)
            return 1;
    }
    return 0;
}
}

#endif // VALVE_SETTING_SORT_POLICY_H
