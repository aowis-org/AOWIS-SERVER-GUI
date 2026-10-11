#pragma once

#include "entity_inspector/coordinate_property_binding.h"
#include "config/numeric_editor_adapters.h"

// The binding base must be constructed before the numeric control which uses
// it. This editor never commits Qt's rounded value on refresh/focus alone.
class CoordinateNumericSpinBox final : private CoordinatePropertyBinding, public NumericDoubleSpinBox
{
public:
    CoordinateNumericSpinBox(CoordinateReader read, CoordinateWriter write,
                             Component component, QWidget *parent = nullptr)
        : CoordinatePropertyBinding(std::move(read), std::move(write), component),
          NumericDoubleSpinBox(static_cast<CoordinatePropertyBinding &>(*this), parent),
          component_(component)
    {
        setAccelerated(true);
    }

    bool refreshCoordinate()
    {
        const double limit = this->component_ == Component::Latitude ? 90.0 : 180.0;
        return refresh({"deg", QStringLiteral(" °"), -limit, limit, 0.000001, 6});
    }

private:
    Component component_;
};
