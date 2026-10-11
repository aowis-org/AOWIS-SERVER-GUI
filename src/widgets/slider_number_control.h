#ifndef SLIDER_NUMBER_CONTROL_H
#define SLIDER_NUMBER_CONTROL_H

#include "config/numeric_editor_adapters.h"
#include <QWidget>

// A precise setting with two lossy presentations. Programmatic setters never
// emit valueChanged; only accepted user edits commit and notify callers.
class SliderNumberControl : public QWidget
{
    Q_OBJECT

public:
    explicit SliderNumberControl(QWidget *parent = nullptr);
    ~SliderNumberControl() override;

    void setRange(double minimum, double maximum);
    void setDecimals(int decimals);
    void setSingleStep(double step);
    // Optional slider edit quantum, independent of spin-box display precision.
    // Zero uses the slider's continuous 1000-position mapping.
    void setSliderStep(double step);
    void setSuffix(const QString &suffix);
    void setValue(double value);
    double value() const;

signals:
    void valueChanged(double value);

private:
    double precise_value = 0.0;
    double slider_step = 0.0;
    NumericEditorPresentation presentation;
    NumericPropertyBinding spin_binding;
    NumericPropertyBinding slider_binding;
    NumericSlider *slider = nullptr;
    NumericDoubleSpinBox *spin_box = nullptr;

    void refreshControls();
    bool commitValue(double value, bool from_slider);
};

#endif // SLIDER_NUMBER_CONTROL_H
