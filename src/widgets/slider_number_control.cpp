#include "widgets/slider_number_control.h"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSlider>

#include <cmath>

namespace
{
// QSlider only moves in integer steps; this many slider positions span
// [minimum, maximum] regardless of the spin box's own decimals or range, so
// dragging stays reasonably fine-grained for both a 0-1 fraction and a
// 100-5000 distance.
constexpr int SliderSteps = 1000;
}

SliderNumberControl::SliderNumberControl(QWidget *parent)
    : QWidget(parent)
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    this->slider = new QSlider(Qt::Horizontal, this);
    this->slider->setRange(0, SliderSteps);
    layout->addWidget(this->slider, 1);

    this->spin_box = new QDoubleSpinBox(this);
    this->spin_box->setKeyboardTracking(false);
    this->spin_box->setMinimumWidth(96);
    layout->addWidget(this->spin_box);

    connect(this->slider, &QSlider::valueChanged, this, [this](int position)
    {
        const double new_value = valueFromSliderPosition(position);
        if (!qFuzzyCompare(new_value + 1.0, this->spin_box->value() + 1.0))
        {
            const QSignalBlocker blocker(this->spin_box);
            this->spin_box->setValue(new_value);
        }
        emit valueChanged(this->spin_box->value());
    });

    connect(this->spin_box, &QDoubleSpinBox::valueChanged, this, [this](double new_value)
    {
        updateSliderFromValue(new_value);
        emit valueChanged(new_value);
    });
}

void SliderNumberControl::setRange(double minimum, double maximum)
{
    this->minimum_value = minimum;
    this->maximum_value = qMax(minimum, maximum);

    const QSignalBlocker spin_blocker(this->spin_box);
    this->spin_box->setRange(this->minimum_value, this->maximum_value);
    updateSliderFromValue(this->spin_box->value());
}

void SliderNumberControl::setDecimals(int decimals)
{
    const QSignalBlocker blocker(this->spin_box);
    this->spin_box->setDecimals(decimals);
}

void SliderNumberControl::setSingleStep(double step)
{
    this->spin_box->setSingleStep(step);
}

void SliderNumberControl::setSuffix(const QString &suffix)
{
    this->spin_box->setSuffix(suffix);
}

void SliderNumberControl::setValue(double value)
{
    const QSignalBlocker spin_blocker(this->spin_box);
    this->spin_box->setValue(value);
    updateSliderFromValue(this->spin_box->value());
}

double SliderNumberControl::value() const
{
    return this->spin_box->value();
}

void SliderNumberControl::updateSliderFromValue(double value)
{
    const double span = qMax(1e-9, this->maximum_value - this->minimum_value);
    const double fraction = qBound(0.0, (value - this->minimum_value) / span, 1.0);
    const QSignalBlocker blocker(this->slider);
    this->slider->setValue(int(std::lround(fraction * double(SliderSteps))));
}

double SliderNumberControl::valueFromSliderPosition(int position) const
{
    const double fraction = double(position) / double(SliderSteps);
    return this->minimum_value + fraction * (this->maximum_value - this->minimum_value);
}
