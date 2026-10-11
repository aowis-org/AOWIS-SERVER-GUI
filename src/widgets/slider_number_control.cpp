#include "widgets/slider_number_control.h"

#include <QHBoxLayout>
#include <QSignalBlocker>

#include <algorithm>
#include <cmath>

namespace
{
std::optional<double> identity(double value, const std::string &)
{
    return value;
}
}

SliderNumberControl::SliderNumberControl(QWidget *parent)
    : QWidget(parent),
      spin_binding([this] { return std::optional<double>(this->precise_value); },
                   [this](double value) { return commitValue(value, false); }, identity, identity),
      slider_binding([this] { return std::optional<double>(this->precise_value); },
                     [this](double value) { return commitValue(value, true); }, identity, identity)
{
    this->presentation.maximum = 1.0;
    this->presentation.step = 1.0;
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    this->slider = new NumericSlider(this->slider_binding, this);
    layout->addWidget(this->slider, 1);
    this->spin_box = new NumericDoubleSpinBox(this->spin_binding, this);
    this->spin_box->setMinimumWidth(96);
    layout->addWidget(this->spin_box);
    refreshControls();
}

SliderNumberControl::~SliderNumberControl()
{
    // Adapters hold references to bindings, so destroy them before the members.
    delete this->slider;
    delete this->spin_box;
}

void SliderNumberControl::setRange(double minimum, double maximum)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum))
        return;
    maximum = std::max(minimum, maximum);
    if (!std::isfinite(maximum - minimum))
        return;
    this->presentation.minimum = minimum;
    this->presentation.maximum = maximum;
    refreshControls();
}

void SliderNumberControl::setDecimals(int decimals)
{
    this->presentation.decimals = std::clamp(decimals, 0, 323);
    refreshControls();
}

void SliderNumberControl::setSingleStep(double step)
{
    if (!std::isfinite(step) || step <= 0.0)
        return;
    this->presentation.step = step;
    refreshControls();
}

void SliderNumberControl::setSliderStep(double step)
{
    if (std::isfinite(step) && step >= 0.0)
    {
        this->slider_step = step;
        refreshControls();
    }
}

void SliderNumberControl::setSuffix(const QString &suffix)
{
    this->presentation.suffix = suffix;
    refreshControls();
}

void SliderNumberControl::setValue(double value)
{
    if (!std::isfinite(value))
        return;
    this->precise_value = value;
    // An explicit owner update supersedes a pending draft. Display-only setters
    // above instead preserve the draft through the shared adapter lifecycle.
    this->spin_box->cancelEdit();
    this->slider_binding.cancel();
    refreshControls();
}

double SliderNumberControl::value() const
{
    return this->precise_value;
}

void SliderNumberControl::refreshControls()
{
    this->spin_box->refresh(this->presentation);
    if (this->presentation.minimum == this->presentation.maximum)
    {
        const QSignalBlocker blocker(this->slider);
        this->slider->setRange(0, 0);
        this->slider->setEnabled(false);
    }
    else
    {
        this->slider->refresh(this->presentation);
        // Keyboard/page actions must cross at least one explicit edit quantum;
        // otherwise snapping back to the stored value could trap arrow keys.
        const double span = this->presentation.maximum - this->presentation.minimum;
        const double positions = std::ceil((this->slider_step / span) * 1000.0);
        const int step = static_cast<int>(std::clamp(positions, 1.0, 1000.0));
        this->slider->setSingleStep(step);
        this->slider->setPageStep(std::max(10, step));
    }
}

bool SliderNumberControl::commitValue(double value, bool from_slider)
{
    if (!std::isfinite(value) || value < this->presentation.minimum
        || value > this->presentation.maximum)
        return false;
    if (from_slider && this->slider_step > 0.0)
    {
        const double steps = (value - this->presentation.minimum) / this->slider_step;
        if (!std::isfinite(steps))
            return false;
        value = std::clamp(this->presentation.minimum + std::round(steps) * this->slider_step,
                           this->presentation.minimum, this->presentation.maximum);
    }
    if (value == this->precise_value)
        return true;
    this->precise_value = value;
    // Refresh only the peer here. The initiating adapter completes its own
    // transaction and refresh after the writer returns.
    if (from_slider)
        this->spin_box->cancelEdit();
    else
    {
        this->slider_binding.cancel();
        refreshControls();
    }
    emit valueChanged(this->precise_value);
    return true;
}
