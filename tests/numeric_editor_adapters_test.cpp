#include "config/numeric_editor_adapters.h"
#include <QApplication>
#include <QKeyEvent>
#include <QFocusEvent>
#include "config/unit_aware_view.h"
#include <stdexcept>
#include <string>

static void requireAt(bool condition, const char *expression, int line) {
    if (!condition)
        throw std::runtime_error(std::string("numeric editor regression at line ") +
                                 std::to_string(line) + ": " + expression);
}
#define require(condition) requireAt((condition), #condition, __LINE__)
static void type(QLineEdit &editor, const QString &text) {
    editor.selectAll();
    QKeyEvent event(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    QApplication::sendEvent(&editor, &event);
}
static NumericPropertyBinding bindingFor(double &value, int &writes) {
    return NumericPropertyBinding([&value] { return std::optional<double>(value); },
        [&value, &writes](double candidate) { value = candidate; ++writes; return true; },
        [](double candidate, const std::string &unit) { return std::optional<double>(candidate * (unit == "scaled" ? 10.0 : 1.0)); },
        [](double candidate, const std::string &unit) { return std::optional<double>(candidate / (unit == "scaled" ? 10.0 : 1.0)); });
}
void runSliderNumberControlTests();

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    runSliderNumberControlTests();
    double value = 4.057389123;
    const double original = value;
    int writes = 0;
    NumericPropertyBinding binding = bindingFor(value, writes);
    NumericDoubleSpinBox spin(binding);
    NumericEditorPresentation settings{"native", QStringLiteral(" m"), -100.0, 100.0, 0.1, 2};
    require(spin.refresh(settings));
    require(spin.value() == 4.06 && value == original && writes == 0);
    require(spin.commitEdit() == NumericPropertyBinding::Result::Unchanged);
    spin.setValue(9.0); // Public programmatic API is never a user edit.
    require(spin.commitEdit() == NumericPropertyBinding::Result::Unchanged && writes == 0);
    spin.refresh(settings);
    QLineEdit *text = spin.findChild<QLineEdit *>();
    require(text != nullptr);
    type(*text, QStringLiteral("4.06"));
    require(spin.commitEdit() == NumericPropertyBinding::Result::Unchanged && value == original);
    type(*text, QStringLiteral("5.25"));
    settings.unit = "scaled";
    settings.suffix = QStringLiteral(" scaled");
    require(spin.refresh(settings));
    require(spin.suffix() == QStringLiteral(" m")); // Pending edit retains old units.
    require(spin.commitEdit() == NumericPropertyBinding::Result::Committed);
    require(value == 5.25 && spin.value() == 52.5 && writes == 1);
    type(*text, QStringLiteral("-"));
    require(spin.commitEdit() == NumericPropertyBinding::Result::Invalid && writes == 1);
    spin.cancelEdit();
    settings.maximum = 1.0;
    require(spin.refresh(settings)); // Display clamp cannot change model.
    require(value == 5.25 && writes == 1);

    value = original;
    NumericPropertyBinding integer_binding = bindingFor(value, writes);
    NumericIntegerSpinBox integer(integer_binding);
    settings = {"native", {}, -100.0, 100.0, 1.0, 0};
    require(integer.refresh(settings) && integer.value() == 4 && value == original);
    integer.stepUp(); // Explicit stepping enters through stepBy.
    require(value == 5.0 && writes == 2);

    value = original;
    NumericPropertyBinding slider_binding = bindingFor(value, writes);
    NumericSlider slider(slider_binding);
    settings = {"native", {}, 0.0, 10.0, 1.0, 2};
    require(slider.refresh(settings, 100) && slider.value() == 41);
    slider.setValue(80);
    require(value == original && writes == 2);
    slider.refresh(settings, 100);
    slider.triggerAction(QAbstractSlider::SliderSingleStepAdd);
    require(std::abs(value - 4.2) < 1e-12 && writes == 3);
    require(!slider.refresh(settings, 0));

    value = original;
    NumericPropertyBinding line_binding = bindingFor(value, writes);
    NumericLineEdit line(line_binding);
    line.setLocale(QLocale(QLocale::German));
    require(line.refresh("native", 3));
    require(line.commitEdit() == NumericPropertyBinding::Result::Unchanged && value == original);
    type(line, QStringLiteral("6,25"));
    line.refresh("scaled", 3);
    require(line.commitEdit() == NumericPropertyBinding::Result::Committed && value == 6.25);
    type(line, QStringLiteral("-"));
    require(line.commitEdit() == NumericPropertyBinding::Result::Invalid);
    line.cancelEdit();
    type(line, QStringLiteral("70"));
    value = 8.0; // Simulate an external command during editing.
    require(line.commitEdit() == NumericPropertyBinding::Result::Conflict && value == 8.0);
    line.cancelEdit();
    // Profile changes flow through the real view lifecycle. An incomplete edit
    // survives focus loss and hidden generations without changing its unit.
    NumericPropertyBinding transaction_binding = bindingFor(value, writes);
    NumericDoubleSpinBox transaction(transaction_binding);
    UnitPresentationContext context;
    context.initialize(QJsonObject{{QStringLiteral("length"), QStringLiteral("native")}});
    NumericEditorPresentation transaction_settings{"native", QStringLiteral(" m"), -100.0, 100.0, 0.1, 2};
    UnitAwareView view(&transaction, context, [&] {
        transaction_settings.unit = context.unit(QStringLiteral("length")).toStdString();
        transaction_settings.suffix = QString::fromStdString(transaction_settings.unit);
        transaction.refresh(transaction_settings);
    });
    transaction.show();
    QLineEdit *transaction_text = transaction.findChild<QLineEdit *>();
    require(transaction_text != nullptr);
    const int before_transaction = writes;
    type(*transaction_text, QStringLiteral("-"));
    const QString incomplete = transaction_text->text();
    context.update(QJsonObject{{QStringLiteral("length"), QStringLiteral("scaled")}});
    require(transaction.displayUnit() == "native");
    QFocusEvent blur(QEvent::FocusOut, Qt::OtherFocusReason);
    QApplication::sendEvent(&transaction, &blur);
    require(transaction_text->text() == incomplete && writes == before_transaction);
    require(transaction.lastResult() == NumericPropertyBinding::Result::Invalid);
    transaction.stepUp();
    require(transaction_text->text() == incomplete && writes == before_transaction);
    transaction.hide();
    context.update(QJsonObject{{QStringLiteral("length"), QStringLiteral("native")}});
    context.update(QJsonObject{{QStringLiteral("length"), QStringLiteral("scaled")}});
    transaction.show();
    require(transaction_text->text() == incomplete);
    require(transaction.displayUnit() == "native");
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&transaction, &escape);
    require(transaction.displayUnit() == "scaled" && writes == before_transaction);
    type(*transaction_text, QStringLiteral("65"));
    context.update(QJsonObject{{QStringLiteral("length"), QStringLiteral("native")}});
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&transaction, &enter);
    require(value == 6.5 && writes == before_transaction + 1);
    require(transaction.displayUnit() == "native");
    QApplication::sendEvent(&transaction, &blur);
    require(writes == before_transaction + 1); // No duplicate focus-out write.

    // Line-edit cancellation also adopts the newest presentation directly.
    line.refresh("native");
    type(line, QStringLiteral("-"));
    line.refresh("scaled");
    QApplication::sendEvent(&line, &escape);
    require(line.displayUnit() == "scaled" && writes == before_transaction + 1);

    // A drag keeps its initial conversion and range until release.
    value = 4.0;
    slider.refresh(settings, 100);
    slider.setSliderDown(true);
    NumericEditorPresentation scaled_slider{"scaled", {}, 0.0, 100.0, 1.0, 2};
    slider.refresh(scaled_slider, 100);
    slider.setSliderPosition(50);
    require(value == 5.0);
    slider.setSliderPosition(60);
    require(value == 6.0);
    slider.setSliderDown(false);
    slider.triggerAction(QAbstractSlider::SliderSingleStepAdd);
    require(std::abs(value - 6.1) < 1e-12);
    // Formatting and locale switches cannot reinterpret pending input.
    line.setLocale(QLocale(QLocale::German, QLocale::Germany));
    NumericFormatPolicy grouped{NumericFormatPolicy::Notation::Fixed, 2, true, true};
    line.refresh("native", grouped);
    type(line, QStringLiteral("1.234,50"));
    const int before_format_commit = writes;
    line.setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
    NumericFormatPolicy compact{NumericFormatPolicy::Notation::Significant, 6, false, true};
    line.refresh("native", compact);
    require(line.text() == QStringLiteral("1.234,50"));
    require(line.commitEdit() == NumericPropertyBinding::Result::Committed);
    require(value == 1234.5 && writes == before_format_commit + 1);
    require(line.text() == QStringLiteral("1234.5"));
    line.refresh("native", grouped);
    require(writes == before_format_commit + 1);
    // A clean double editor must step from the precise stored value, not
    // from Qt's rounded 2-decimal value. Integer controls still step normally.
    double hidden_fraction_value = 4.057389123;
    int hidden_fraction_writes = 0;
    NumericPropertyBinding hidden_fraction_binding = bindingFor(
        hidden_fraction_value, hidden_fraction_writes);
    NumericDoubleSpinBox hidden_fraction_editor(hidden_fraction_binding);
    NumericEditorPresentation hidden_fraction_settings{
        "native", {}, 0.0, 100.0, 0.1, 2};
    require(hidden_fraction_editor.refresh(hidden_fraction_settings));
    require(hidden_fraction_editor.value() == 4.06);
    hidden_fraction_editor.stepUp();
    require(hidden_fraction_editor.lastResult() == NumericPropertyBinding::Result::Committed);
    require(std::abs(hidden_fraction_value - 4.157389123) < 1e-12);
    hidden_fraction_editor.stepDown();
    require(std::abs(hidden_fraction_value - 4.057389123) < 1e-12);
    require(hidden_fraction_writes == 2);
    return 0;
}
