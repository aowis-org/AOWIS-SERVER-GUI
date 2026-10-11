#pragma once

#include "entity_inspector/emitter_coefficient_binding.h"
#include "config/numeric_editor_adapters.h"
#include "config/unit_presentation_context.h"

// Do not subscribe each editor to profile notifications: the parent demand
// presentation region owns lazy unit invalidation.
class EmitterCoefficientNumericSpinBox final
    : private EmitterCoefficientBindingStorage, public NumericDoubleSpinBox
{
public:
    EmitterCoefficientNumericSpinBox(Reader coefficient, Writer writer, Reader exponent,
                                     QWidget *parent = nullptr)
        : EmitterCoefficientBindingStorage(std::move(coefficient), std::move(writer),
                                           std::move(exponent)),
          NumericDoubleSpinBox(EmitterCoefficientBindingStorage::binding(), parent) {}

    bool refreshUnits(const UnitPresentationContext &context)
    {
        const std::optional<double> exponent = this->read_exponent_();
        if (!exponent) { setEnabled(false); return false; }
        const EmitterCoefficientUnits::Unit unit{
            context.unit(QStringLiteral("volumetric_flow_rate")).toStdString(),
            context.unit(QStringLiteral("pressure_head")).toStdString(), *exponent};
        const std::optional<std::string> token = EmitterCoefficientUnits::encode(unit);
        const std::optional<double> maximum = EmitterCoefficientUnits::toDisplay(1e9, unit);
        const std::optional<double> step = EmitterCoefficientUnits::toDisplay(0.001, unit);
        if (!token || !maximum || !step || *step <= 0.0) {
            setEnabled(false);
            return false;
        }
        const NumericFormatPolicy format = context.formatPolicy(QStringLiteral("volumetric_flow_rate"));
        const int decimals = format.notation == NumericFormatPolicy::Notation::Fixed
            ? format.precision : 10;
        return refresh({*token, QString(), 0.0, *maximum, *step,
                        decimals, format.grouping, context.locale()});
    }

    QString displayedUnitLabel() const
    {
        const std::optional<EmitterCoefficientUnits::Unit> unit =
            EmitterCoefficientUnits::decode(displayUnit());
        if (!unit) return QStringLiteral("—");
        return QStringLiteral("%1 / (%2)^%3")
            .arg(QString::fromStdString(unit->flow), QString::fromStdString(unit->head),
                 QLocale::c().toString(unit->exponent, 'g', 17));
    }
};
