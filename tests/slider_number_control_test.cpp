#include "widgets/slider_number_control.h"
#include "unit_test_check.h"

#include <QApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
void checkExact(double actual, double expected)
{
    CHECK(std::memcmp(&actual, &expected, sizeof actual) == 0);
}

void typeNumber(QDoubleSpinBox *spin, const QString &text)
{
    QLineEdit *editor = spin->findChild<QLineEdit *>();
    CHECK(editor != nullptr);
    editor->selectAll();
    QKeyEvent typing(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    QApplication::sendEvent(editor, &typing);
}

void finishEdit(QDoubleSpinBox *spin)
{
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(spin, &enter);
}
}

void runSliderNumberControlTests()
{
    SliderNumberControl control;
    QSlider *slider = control.findChild<QSlider *>();
    QDoubleSpinBox *spin = control.findChild<QDoubleSpinBox *>();
    CHECK(slider != nullptr && spin != nullptr);
    int writes = 0;
    double notified = 0.0;
    QObject::connect(&control, &SliderNumberControl::valueChanged,
                     [&writes, &notified](double value) { ++writes; notified = value; });
    const double original = 0.123456789012345;
    control.setRange(0.0, 1.0);
    control.setDecimals(2);
    control.setValue(original);
    CHECK(spin->value() == 0.12);
    checkExact(control.value(), original);
    for (int i = 0; i < 100; ++i)
    {
        control.setDecimals(i % 2 == 0 ? 0 : 6);
        control.setRange(0.0, 0.1); // Clamp presentation only.
        control.setSuffix(QStringLiteral(" px"));
        control.setSingleStep(0.01);
        checkExact(control.value(), original);
        control.setRange(0.0, 1.0);
    }
    control.setRange(0.2, 0.2);
    CHECK(!slider->isEnabled());
    checkExact(control.value(), original);
    control.setRange(0.0, 1.0);
    CHECK(slider->isEnabled());
    control.setDecimals(2);
    control.setSuffix(QString());
    CHECK(writes == 0);

    QFocusEvent focus_out(QEvent::FocusOut);
    QApplication::sendEvent(spin, &focus_out);
    typeNumber(spin, QStringLiteral("0.12"));
    finishEdit(spin);
    checkExact(control.value(), original);
    CHECK(writes == 0);
    // Programmatic child refreshes are never edits either.
    spin->setValue(0.7);
    slider->setValue(700);
    checkExact(control.value(), original);
    CHECK(writes == 0);
    control.setValue(original);
    typeNumber(spin, QStringLiteral("0.25"));
    finishEdit(spin);
    CHECK(writes == 1 && control.value() == 0.25 && notified == 0.25);
    CHECK(slider->value() == 250);
    control.setSingleStep(0.1);
    spin->stepUp();
    CHECK(writes == 2);
    CHECK(std::abs(control.value() - 0.35) < 1e-12);

    // A slider edit stores its own mapped value, not the rounded spin value.
    control.setValue(0.4);
    control.setDecimals(1);
    slider->triggerAction(QAbstractSlider::SliderSingleStepAdd);
    CHECK(writes == 3);
    CHECK(std::abs(control.value() - 0.401) < 1e-12);
    CHECK(spin->value() == 0.4);
    checkExact(control.value(), notified);

    // Display precision does not control intentional integer slider increments.
    control.setRange(8.0, 19.0);
    control.setSliderStep(1.0);
    control.setDecimals(3);
    control.setValue(12.0);
    const int before_integer_edit = writes;
    slider->triggerAction(QAbstractSlider::SliderToMaximum);
    CHECK(writes == before_integer_edit + 1);
    CHECK(control.value() == 19.0 && notified == 19.0);

    slider->triggerAction(QAbstractSlider::SliderSingleStepSub);
    CHECK(control.value() == 18.0 && writes == before_integer_edit + 2);

    // An unrelated setting save reads the untouched precise value.
    SliderNumberControl other;
    other.setDecimals(2);
    other.setValue(original);
    checkExact(other.value(), original);
    // Invalid setter input cannot poison the authoritative value or mapping.
    other.setValue(std::numeric_limits<double>::quiet_NaN());
    other.setRange(0.0, std::numeric_limits<double>::infinity());
    checkExact(other.value(), original);

    // Presentation changes preserve pending text; an owner update supersedes it.
    control.setRange(0.0, 1.0);
    control.setDecimals(2);
    control.setValue(original);
    const int before_draft = writes;
    typeNumber(spin, QStringLiteral("0.25"));
    control.setDecimals(1);
    CHECK(writes == before_draft);
    finishEdit(spin);
    CHECK(control.value() == 0.25 && writes == before_draft + 1);
    control.setDecimals(2);
    typeNumber(spin, QStringLiteral("0.75"));
    control.setValue(original);
    finishEdit(spin);
    checkExact(control.value(), original);
    CHECK(writes == before_draft + 1);
}
