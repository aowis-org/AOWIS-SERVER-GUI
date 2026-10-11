#pragma once

#include "config/numeric_property_binding.h"
#include <aowis/model/gis.h>

#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <utility>

// WGS84 coordinates are stored in degrees, independently of the display unit
// profile. A single coordinate component can be edited without round-tripping
// the other component (or altitude) through a rounded GUI control.
class CoordinatePropertyBinding : public NumericPropertyBinding
{
public:
    enum class Component { Latitude, Longitude };
    using CoordinateReader = std::function<std::optional<CoordinateWGS84>()>;
    using CoordinateWriter = std::function<bool(const CoordinateWGS84 &)>;

    CoordinatePropertyBinding(CoordinateReader read, CoordinateWriter write, Component component)
        : NumericPropertyBinding(
            [read, component]() -> std::optional<double> {
                const std::optional<CoordinateWGS84> coordinate = read();
                if (!coordinate) return std::nullopt;
                return component == Component::Latitude
                    ? coordinate->latitude_deg : coordinate->longitude_deg;
            },
            [read, write, component](double value) -> bool {
                // Read the full coordinate at commit time. The companion value
                // may have changed since the editor last displayed it.
                std::optional<CoordinateWGS84> coordinate = read();
                if (!coordinate) return false;
                if (component == Component::Latitude)
                    coordinate->latitude_deg = value;
                else
                    coordinate->longitude_deg = value;
                return write(*coordinate);
            },
            [](double value, const std::string &unit) -> std::optional<double> {
                return unit == "deg" ? std::optional<double>(value) : std::nullopt;
            },
            [](double value, const std::string &unit) -> std::optional<double> {
                return unit == "deg" ? std::optional<double>(value) : std::nullopt;
            },
            [component](double value) {
                const double limit = component == Component::Latitude ? 90.0 : 180.0;
                return std::isfinite(value) && value >= -limit && value <= limit;
            }) {}
};
