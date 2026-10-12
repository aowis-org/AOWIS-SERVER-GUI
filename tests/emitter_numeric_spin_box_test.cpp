#include "entity_inspector/emitter_numeric_spin_box.h"
#include "config/unit_numeric_spin_box.h"

#include <QApplication>
#include <QJsonObject>
#include <QLineEdit>
#include <QKeyEvent>
#include <cmath>
#include <functional>
#include <cstdio>
#include <cstdlib>

#define CHECK(condition) do { \
    if (!(condition)) { std::fprintf(stderr, "FAIL: %s at %s:%d\n", #condition, __FILE__, __LINE__); std::exit(1); } \
} while (false)

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    double canonical_coefficient = 1.23456789012345;
    double exponent = 0.5678901234567;
    int coefficient_writes = 0;
    int exponent_writes = 0;
    const std::function<std::optional<double>()> read_coefficient = [&]() -> std::optional<double> { return canonical_coefficient; };
    const std::function<std::optional<double>()> read_exponent = [&]() -> std::optional<double> { return exponent; };
    EmitterCoefficientNumericSpinBox coefficient(
        read_coefficient,
        [&](double value) { canonical_coefficient = value; ++coefficient_writes; return true; },
        read_exponent);
    UnitNumericSpinBox exponent_control(
        read_exponent,
        [&](double value) { exponent = value; ++exponent_writes; return true; },
        "emitter_exponent", "1", 0.000001, 1000000.0, 0.01, 10,
        nullptr, [](const QString &) { return QString(); });
    UnitPresentationContext context;
    (void)context.setLocale(QLocale::c());
    context.initialize(QJsonObject{{"volumetric_flow_rate", "m3/h"},
                                   {"pressure_head", "m"}, {"emitter_exponent", "1"}});
    CHECK(coefficient.refreshUnits(context));
    CHECK(exponent_control.refreshUnits(context));
    CHECK(coefficient_writes == 0 && exponent_writes == 0);
    CHECK(canonical_coefficient == 1.23456789012345 && exponent == 0.5678901234567);

    CHECK(context.update(QJsonObject{{"volumetric_flow_rate", "US gal/min"},
                                     {"pressure_head", "ft"}, {"emitter_exponent", "1"}}));
    CHECK(coefficient.refreshUnits(context));
    CHECK(exponent_control.refreshUnits(context));
    CHECK(coefficient.displayedUnitLabel().contains(QStringLiteral("US gal/min")));
    CHECK(coefficient.displayedUnitLabel().contains(QStringLiteral("ft")));
    CHECK(canonical_coefficient == 1.23456789012345 && exponent == 0.5678901234567);
    CHECK(coefficient_writes == 0 && exponent_writes == 0);
    CHECK(context.update(QJsonObject{{"volumetric_flow_rate", "US gal/min"},
                                     {"pressure_head", "psi"}, {"emitter_exponent", "1"}}));
    CHECK(context.setSpecificGravity(1.2));
    CHECK(coefficient.refreshUnits(context));
    CHECK(coefficient.displayedUnitLabel().contains(QStringLiteral("psi")));
    CHECK(coefficient_writes == 0 && exponent_writes == 0);
    // Merely selecting/focusing and leaving a rounded editor unchanged is read-only.
    coefficient.show();
    coefficient.setFocus();
    QApplication::processEvents();
    coefficient.clearFocus();
    QApplication::processEvents();
    CHECK(coefficient_writes == 0);

    // Text entered by the user must commit using the selected unit profile.
    QLineEdit *line = coefficient.findChild<QLineEdit *>();
    CHECK(line != nullptr);
    coefficient.setFocus();
    line->selectAll();
    QKeyEvent input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier,
                    QStringLiteral("24.25"));
    QApplication::sendEvent(line, &input);
    CHECK(coefficient.commitEdit() == NumericPropertyBinding::Result::Committed);
    const std::optional<double> expected = EmitterCoefficientUnits::toCanonical(
        24.25, {"US gal/min", "psi", exponent, 1.2});
    CHECK(expected);
    CHECK(std::abs(canonical_coefficient - *expected) < 1e-12);
    CHECK(coefficient_writes == 1);

    // Editing the exponent must not alter the stored coefficient.
    QLineEdit *exponent_line = exponent_control.findChild<QLineEdit *>();
    CHECK(exponent_line);
    exponent_control.show();
    exponent_control.setFocus();
    exponent_line->selectAll();
    QKeyEvent exponent_input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier,
                             QStringLiteral("0.75"));
    QApplication::sendEvent(exponent_line, &exponent_input);
    CHECK(exponent_control.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(exponent == 0.75 && exponent_writes == 1);
    CHECK(coefficient_writes == 1);
    CHECK(coefficient.refreshUnits(context));
    CHECK(coefficient_writes == 1);
    return 0;
}
