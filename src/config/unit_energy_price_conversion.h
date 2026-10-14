#pragma once

#include <aowis/model/units/quantity.h>

#include <aowis/model/units/conversion.h>

#include <cmath>
#include <optional>
#include <string_view>

// Currency remains unchanged. Only the energy denominator is converted.
// AOWIS stores the price as currency per kW.h.
namespace UnitEnergyPriceConversion
{
inline std::optional<double> fromStored(double price_per_kwh, std::string_view energy_unit)
{
    if (!std::isfinite(price_per_kwh) || price_per_kwh < 0.0)
        return std::nullopt;
    const std::optional<double> kwh_per_unit =
        aowis::units::convert(AOWIS_QUANTITY("energy"), 1.0, energy_unit, "kW.h");
    if (!kwh_per_unit)
        return std::nullopt;
    const double result = price_per_kwh * *kwh_per_unit;
    return std::isfinite(result) ? std::optional<double>(result) : std::nullopt;
}

inline std::optional<double> toStored(double price_per_unit, std::string_view energy_unit)
{
    if (!std::isfinite(price_per_unit) || price_per_unit < 0.0)
        return std::nullopt;
    const std::optional<double> units_per_kwh =
        aowis::units::convert(AOWIS_QUANTITY("energy"), 1.0, "kW.h", energy_unit);
    if (!units_per_kwh)
        return std::nullopt;
    const double result = price_per_unit * *units_per_kwh;
    return std::isfinite(result) ? std::optional<double>(result) : std::nullopt;
}
}
