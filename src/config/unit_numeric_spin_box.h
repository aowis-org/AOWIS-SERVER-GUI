#pragma once

#include "numeric_editor_adapters.h"
#include "unit_presentation_context.h"
#include <aowis/model/units/conversion.h>
#include <functional>
#include <algorithm>
#include <cmath>
#include <memory>

// Storage units are explicit: GUI property units need not equal the registry's
// canonical unit. All conversion still goes through the Model registry.
class UnitNumericBindingStorage
{
protected:
    UnitNumericBindingStorage(NumericPropertyBinding::Reader read,
                              NumericPropertyBinding::Writer write,
                              const std::string &quantity, const std::string &storage_unit)
        : specific_gravity_(std::make_shared<double>(1.0)),
          binding_(std::move(read), std::move(write),
            [quantity, storage_unit, gravity = this->specific_gravity_]
            (double value, const std::string &unit) {
                return aowis::units::convert(quantity, value, storage_unit, unit, *gravity);
            },
            [quantity, storage_unit, gravity = this->specific_gravity_]
            (double value, const std::string &unit) {
                return aowis::units::convert(quantity, value, unit, storage_unit, *gravity);
            }) {}
    std::shared_ptr<double> specific_gravity_;
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
        // A project change invalidates any draft interpreted with the old
        // specific gravity. Never commit a pressure edit under a new fluid.
        if (*this->specific_gravity_ != context.specificGravity()) {
            if (UnitNumericBindingStorage::binding_.state() != NumericPropertyBinding::State::Clean)
                cancelEdit();
            *this->specific_gravity_ = context.specificGravity();
        }
        const double gravity = *this->specific_gravity_;
        const std::optional<double> minimum = aowis::units::convert(this->quantity_, this->minimum_, this->storage_unit_, unit.toStdString(), gravity);
        const std::optional<double> maximum = aowis::units::convert(this->quantity_, this->maximum_, this->storage_unit_, unit.toStdString(), gravity);
        const std::optional<double> step = aowis::units::convert(this->quantity_, this->step_, this->storage_unit_, unit.toStdString(), gravity);
        if (!minimum || !maximum || !step) { setEnabled(false); return false; }
        // The declared precision is in the stored unit, not the displayed one.
        // Keep at least the same physical editing resolution after conversion
        // (for example, 0.1 mm requires four decimals when displayed in m).
        const std::optional<double> factor = aowis::units::convert(
            this->quantity_, 1.0, this->storage_unit_, unit.toStdString(), gravity);
        if (!factor || !std::isfinite(*factor) || *factor <= 0.0) {
            setEnabled(false);
            return false;
        }
        const double extra_digits = std::max(0.0, std::ceil(-std::log10(*factor)));
        const double step_digits = std::max(0.0, std::ceil(-std::log10(*step)));
        const int required_decimals = static_cast<int>(std::min(323.0,
            std::max(static_cast<double>(this->decimals_) + extra_digits, step_digits)));
        const NumericFormatPolicy policy = context.formatPolicy(quantity);
        const int decimals = policy.notation == NumericFormatPolicy::Notation::Fixed
            ? std::max(policy.precision, required_decimals) : required_decimals;
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
                                              this->storage_unit_, unit.toStdString(), *this->specific_gravity_);
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
