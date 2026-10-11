#pragma once

#include "config/numeric_property_binding.h"
#include <aowis/model/units/conversion.h>

#include <charconv>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// Q = C * h^n, with the AOWIS storage convention C in (m3/h)/m^n.
// This is not a fixed-dimension quantity: each presentation conversion must
// use the junction's current exponent and both the selected flow/head units.
namespace EmitterCoefficientUnits
{
struct Unit
{
    std::string flow;
    std::string head;
    double exponent = 0.0;
};

inline std::optional<std::string> encode(const Unit &unit)
{
    if (unit.flow.empty() || unit.head.empty() || unit.flow.find('|') != std::string::npos ||
        unit.head.find('|') != std::string::npos || !std::isfinite(unit.exponent) ||
        unit.exponent <= 0.0)
        return std::nullopt;
    char buffer[64] = {};
    const std::to_chars_result result = std::to_chars(
        buffer, buffer + sizeof(buffer), unit.exponent, std::chars_format::general,
        std::numeric_limits<double>::max_digits10);
    if (result.ec != std::errc{}) return std::nullopt;
    return unit.flow + "|" + unit.head + "|" + std::string(buffer, result.ptr);
}

inline std::optional<Unit> decode(const std::string &key)
{
    const std::size_t first = key.find('|');
    if (first == std::string::npos) return std::nullopt;
    const std::size_t second = key.find('|', first + 1);
    if (second == std::string::npos || key.find('|', second + 1) != std::string::npos)
        return std::nullopt;
    Unit unit{key.substr(0, first), key.substr(first + 1, second - first - 1), 0.0};
    const char *begin = key.data() + second + 1;
    const std::from_chars_result result = std::from_chars(
        begin, key.data() + key.size(), unit.exponent, std::chars_format::general);
    if (result.ec != std::errc{} || result.ptr != key.data() + key.size() ||
        unit.flow.empty() || unit.head.empty() || !std::isfinite(unit.exponent) ||
        unit.exponent <= 0.0)
        return std::nullopt;
    return unit;
}

inline std::optional<double> flowAndHeadScale(const Unit &unit)
{
    const std::optional<double> flow = aowis::units::convert(
        "volumetric_flow_rate", 1.0, "m3/h", unit.flow);
    const std::optional<double> head = aowis::units::convert(
        "pressure_head", 1.0, "m", unit.head);
    if (!flow || !head || !std::isfinite(*flow) || *flow <= 0.0 ||
        !std::isfinite(*head) || *head <= 0.0 ||
        !std::isfinite(unit.exponent) || unit.exponent <= 0.0)
        return std::nullopt;
    const double scale = *flow / std::pow(*head, unit.exponent);
    if (!std::isfinite(scale) || scale <= 0.0)
        return std::nullopt;
    return scale;
}

inline std::optional<double> toDisplay(double canonical, const Unit &unit)
{
    const std::optional<double> scale = flowAndHeadScale(unit);
    if (!scale || !std::isfinite(canonical)) return std::nullopt;
    const double result = canonical * *scale;
    return std::isfinite(result) ? std::optional<double>(result) : std::nullopt;
}

inline std::optional<double> toCanonical(double displayed, const Unit &unit)
{
    const std::optional<double> scale = flowAndHeadScale(unit);
    if (!scale || !std::isfinite(displayed)) return std::nullopt;
    const double result = displayed / *scale;
    return std::isfinite(result) ? std::optional<double>(result) : std::nullopt;
}
}

// Extends the generic edit transaction with an exponent guard. An exponent
// change after the user typed a coefficient invalidates the pending commit,
// rather than interpreting C with a different physical dimension.
class EmitterCoefficientBindingStorage
{
public:
    using Reader = NumericPropertyBinding::Reader;
    using Writer = NumericPropertyBinding::Writer;

    EmitterCoefficientBindingStorage(Reader coefficient, Writer writer, Reader exponent)
        : read_exponent_(std::move(exponent)),
          converted_exponent_(std::make_shared<std::optional<double>>()),
          binding_(std::move(coefficient),
              [writer = std::move(writer), read_exponent = this->read_exponent_,
               captured = this->converted_exponent_](double canonical) -> bool {
                  const std::optional<double> current = read_exponent();
                  return current && *captured && *current == **captured && writer(canonical);
              },
              [](double value, const std::string &token) -> std::optional<double> {
                  const std::optional<EmitterCoefficientUnits::Unit> unit =
                      EmitterCoefficientUnits::decode(token);
                  return unit ? EmitterCoefficientUnits::toDisplay(value, *unit) : std::nullopt;
              },
              [read_exponent = this->read_exponent_, captured = this->converted_exponent_]
              (double value, const std::string &token) -> std::optional<double> {
                  captured->reset();
                  const std::optional<EmitterCoefficientUnits::Unit> unit =
                      EmitterCoefficientUnits::decode(token);
                  const std::optional<double> current = read_exponent();
                  if (!unit || !current || *current != unit->exponent)
                      return std::nullopt;
                  const std::optional<double> canonical =
                      EmitterCoefficientUnits::toCanonical(value, *unit);
                  if (canonical) *captured = unit->exponent;
                  return canonical;
              },
              [](double value) { return std::isfinite(value) && value >= 0.0; }) {}

    NumericPropertyBinding &binding() { return this->binding_; }

protected:
    Reader read_exponent_;
    std::shared_ptr<std::optional<double>> converted_exponent_;
    NumericPropertyBinding binding_;
};
