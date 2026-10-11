#include "entity_inspector/coordinate_numeric_spin_box.h"

#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <cstdlib>
#include <cstring>
#include <iostream>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "Failure at line " << __LINE__ << ": " #condition "\n"; std::abort(); } } while (false)

static void typeValue(QLineEdit &editor, const QString &value)
{
    editor.selectAll();
    QKeyEvent event(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, value);
    QApplication::sendEvent(&editor, &event);
}

static void checkEditor(bool demand_point)
{
    CoordinateWGS84 model;
    model.latitude_deg = 12.12345678912345;
    model.longitude_deg = 18.98765432198765;
    model.altitude_m = 123.456789123456;
    const CoordinateWGS84 original = model;
    int writes = 0;
    CoordinateNumericSpinBox *latitude_editor = nullptr;
    CoordinateNumericSpinBox *longitude_editor = nullptr;

    const CoordinatePropertyBinding::CoordinateReader read = [&]() {
        return std::optional<CoordinateWGS84>(model);
    };
    const CoordinatePropertyBinding::CoordinateWriter write = [&](const CoordinateWGS84 &next) {
        model = next;
        ++writes;
        // Model mutation emits synchronous changed signals in production.
        if (latitude_editor != nullptr) latitude_editor->refreshCoordinate();
        if (longitude_editor != nullptr) longitude_editor->refreshCoordinate();
        return true;
    };

    CoordinateNumericSpinBox latitude(read, write, CoordinatePropertyBinding::Component::Latitude);
    CoordinateNumericSpinBox longitude(read, write, CoordinatePropertyBinding::Component::Longitude);
    latitude_editor = &latitude;
    longitude_editor = &longitude;
    latitude.setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    longitude.setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    CHECK(latitude.refreshCoordinate() && longitude.refreshCoordinate());
    CHECK(latitude.value() == 12.123457);
    CHECK(longitude.value() == 18.987654);
    CHECK(latitude.suffix() == QStringLiteral(" °"));
    CHECK(longitude.suffix() == QStringLiteral(" °"));
    CHECK(latitude.minimum() == -90.0 && latitude.maximum() == 90.0);
    CHECK(longitude.minimum() == -180.0 && longitude.maximum() == 180.0);
    CHECK(writes == 0 && model.latitude_deg == original.latitude_deg);
    CHECK(model.longitude_deg == original.longitude_deg);

    // Qt value changes, showing/hiding, and focus/commit without an edit are
    // strictly presentation operations.
    latitude.setValue(17.0);
    CHECK(latitude.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(latitude.refreshCoordinate());
    CHECK(latitude.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(writes == 0 && model.latitude_deg == original.latitude_deg);
    CHECK(model.longitude_deg == original.longitude_deg);

    QLineEdit *latitude_text = latitude.findChild<QLineEdit *>();
    QLineEdit *longitude_text = longitude.findChild<QLineEdit *>();
    CHECK(latitude_text != nullptr && longitude_text != nullptr);
    typeValue(*latitude_text, QStringLiteral("12.123457"));
    CHECK(latitude.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    CHECK(writes == 0 && model.latitude_deg == original.latitude_deg);
    CHECK(model.longitude_deg == original.longitude_deg);
    typeValue(*latitude_text, QStringLiteral("12.125"));
    CHECK(latitude.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(writes == 1 && model.latitude_deg == 12.125);
    CHECK(std::memcmp(&model.longitude_deg, &original.longitude_deg, sizeof(double)) == 0);
    CHECK(model.altitude_m == original.altitude_m);

    // Editing the other control must likewise preserve its companion's full precision.
    model.latitude_deg = original.latitude_deg;
    CHECK(latitude.refreshCoordinate());
    typeValue(*longitude_text, QStringLiteral("19.125"));
    CHECK(longitude.commitEdit() == NumericPropertyBinding::Result::Committed);
    CHECK(writes == 2 && model.longitude_deg == 19.125);
    CHECK(std::memcmp(&model.latitude_deg, &original.latitude_deg, sizeof(double)) == 0);
    CHECK(model.altitude_m == original.altitude_m);

    // A refreshed neighbor is not a request to overwrite its exact value.
    CHECK(latitude.refreshCoordinate() && longitude.refreshCoordinate());
    CHECK(writes == 2);
    latitude.stepUp();  // Only an explicit step can quantize the edited component.
    CHECK(writes == 3 && model.latitude_deg == 12.123458);
    CHECK(model.longitude_deg == 19.125 && model.altitude_m == original.altitude_m);
    if (demand_point)
        std::cout << "Demand Point coordinate spin boxes passed\n";
    else
        std::cout << "Hydraulic-node coordinate spin boxes passed\n";
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    checkEditor(false);
    checkEditor(true);
    return 0;
}
