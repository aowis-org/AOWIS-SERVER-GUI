#pragma once

#include "numeric_editor_adapters.h"
#include "unit_presentation_context.h"
#include <aowis/model/units/conversion.h>
#include <functional>

// Storage units are explicit: GUI property units need not equal the registry's
// canonical unit. All conversion still goes through the Model registry.
class UnitNumericBindingStorage
{
protected:
    UnitNumericBindingStorage(NumericPropertyBinding::Reader read,
                              NumericPropertyBinding::Writer write,
                              const std::string &quantity, const std::string &storage_unit)
        : binding_(std::move(read), std::move(write),
            [quantity, storage_unit](double value, const std::string &unit) {
                return aowis::units::convert(quantity, value, storage_unit, unit);
            },
            [quantity, storage_unit](double value, const std::string &unit) {
                return aowis::units::convert(quantity, value, unit, storage_unit);
            }) {}
    NumericPropertyBinding binding_;
};

// The first base owns the binding, so it outlives the Qt control base.
// No per-control subscription: the containing view calls refreshUnits().
class UnitNumericSpinBox final : private UnitNumericBindingStorage, public NumericDoubleSpinBox
{
public:
    UnitNumericSpinBox(NumericPropertyBinding::Reader read, NumericPropertyBinding::Writer write,
                      std::string quantity, std::string storage_unit,
                      double minimum, double maximum, double step, int decimals,
                      QWidget *parent = nullptr,
                      std::function<QString(const QString &)> suffix_formatter = {})
        : UnitNumericBindingStorage(std::move(read), std::move(write), quantity, storage_unit),
          NumericDoubleSpinBox(UnitNumericBindingStorage::binding_, parent),
          quantity_(std::move(quantity)), storage_unit_(std::move(storage_unit)),
          minimum_(minimum), maximum_(maximum), step_(step), decimals_(decimals),
          suffix_formatter_(std::move(suffix_formatter)) {}

    bool refreshUnits(const UnitPresentationContext &context) {
        const QString quantity = QString::fromStdString(this->quantity_);
        const QString unit = context.unit(quantity);
        const std::optional<double> minimum = aowis::units::convert(this->quantity_, this->minimum_, this->storage_unit_, unit.toStdString());
        const std::optional<double> maximum = aowis::units::convert(this->quantity_, this->maximum_, this->storage_unit_, unit.toStdString());
        const std::optional<double> step = aowis::units::convert(this->quantity_, this->step_, this->storage_unit_, unit.toStdString());
        if (!minimum || !maximum || !step) { setEnabled(false); return false; }
        const NumericFormatPolicy policy = context.formatPolicy(quantity);
        const int decimals = policy.notation == NumericFormatPolicy::Notation::Fixed
            ? policy.precision : this->decimals_;
        const QString suffix = this->suffix_formatter_
            ? this->suffix_formatter_(unit) : NumericText::suffix(unit);
        return refresh({unit.toStdString(), suffix, *minimum, *maximum,
                        *step, decimals, policy.grouping, context.locale()});
    }

    // Some tank geometry controls switch between stored inputs and calculated
    // read-only results. The supplied value is still in the explicit storage
    // unit, never in the active display unit. Showing it cannot commit an edit.
    bool refreshDerived(const UnitPresentationContext &context,
                        const std::optional<double> &storage_value) {
        // A mode change can turn an actively edited field into a calculated
        // value. Abandon that pending presentation edit without a Model write.
        UnitNumericBindingStorage::binding_.cancel();
        if (!refreshUnits(context)) return false;
        const QString unit = context.unit(QString::fromStdString(this->quantity_));
        std::optional<double> displayed;
        if (storage_value && std::isfinite(*storage_value))
            displayed = aowis::units::convert(this->quantity_, *storage_value,
                                              this->storage_unit_, unit.toStdString());
        const QSignalBlocker blocker(this);
        if (!displayed) {
            setSpecialValueText(QStringLiteral("[Not available]"));
            setValue(minimum());
        } else {
            setSpecialValueText(QString());
            setValue(*displayed);
        }
        return displayed.has_value();
    }
private:
    std::string quantity_;
    std::string storage_unit_;
    double minimum_;
    double maximum_;
    double step_;
    int decimals_;
    std::function<QString(const QString &)> suffix_formatter_;
};
