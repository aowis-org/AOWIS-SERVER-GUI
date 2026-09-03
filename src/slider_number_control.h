#ifndef SLIDER_NUMBER_CONTROL_H
#define SLIDER_NUMBER_CONTROL_H

#include <QWidget>

class QDoubleSpinBox;
class QSlider;

// Pairs a QSlider with a QDoubleSpinBox for one numeric setting: dragging
// the slider updates the number, typing a number updates the slider, and
// valueChanged() fires once per genuine change from either side. Used for
// the map performance settings so each entry gets both coarse (slider) and
// precise (typed) control without duplicating the sync logic per setting.
class SliderNumberControl : public QWidget
{
    Q_OBJECT

public:
    explicit SliderNumberControl(QWidget *parent = nullptr);

    void setRange(double minimum, double maximum);
    void setDecimals(int decimals);
    void setSingleStep(double step);
    void setSuffix(const QString &suffix);
    void setValue(double value);
    double value() const;

signals:
    void valueChanged(double value);

private:
    QSlider *slider = nullptr;
    QDoubleSpinBox *spin_box = nullptr;
    double minimum_value = 0.0;
    double maximum_value = 1.0;

    void updateSliderFromValue(double value);
    double valueFromSliderPosition(int position) const;
};

#endif // SLIDER_NUMBER_CONTROL_H
