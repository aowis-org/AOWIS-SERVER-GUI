#pragma once

#include "config/unit_quantity_key.h"

#include "numeric_editor_adapters.h"
#include "unit_presentation_context.h"
#include "unit_energy_price_conversion.h"


#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// A tariff is currency per energy unit, not an energy measurement. The currency
// remains unchanged; only the denominator follows the active energy unit.
// The Model stores pump tariffs in currency/kW.h.

class UnitEnergyPriceBindingStorage
{
protected:
    UnitEnergyPriceBindingStorage(NumericPropertyBinding::Reader read,
                                  NumericPropertyBinding::Writer write)
        : binding_(std::move(read), std::move(write),
            [](double value, const std::string &unit) {
                return UnitEnergyPriceConversion::fromStored(value, unit);
            },
            [](double value, const std::string &unit) {
                return UnitEnergyPriceConversion::toStored(value, unit);
            }) {}

    NumericPropertyBinding binding_;
};

// The binding owner must precede the Qt editor base so its reference stays valid.
// A containing UnitAwareView handles unit-change notifications for this editor.
class UnitEnergyPriceSpinBox final : private UnitEnergyPriceBindingStorage,
                                     public NumericDoubleSpinBox
{
public:
    UnitEnergyPriceSpinBox(NumericPropertyBinding::Reader read,
                           NumericPropertyBinding::Writer write,
                           QWidget *parent = nullptr)
        : UnitEnergyPriceBindingStorage(std::move(read), std::move(write)),
          NumericDoubleSpinBox(UnitEnergyPriceBindingStorage::binding_, parent)
    {
        setSpecialValueText(QStringLiteral("Not priced"));
    }

    bool refreshUnits(const UnitPresentationContext &context, const QString &currency)
    {
        const QString unit = context.unit(AOWIS_QSTRING_QUANTITY("energy"));
        const std::string unit_id = unit.toStdString();
        const std::optional<double> maximum = UnitEnergyPriceConversion::fromStored(1000.0, unit_id);
        const std::optional<double> step = UnitEnergyPriceConversion::fromStored(0.01, unit_id);
        if (!maximum || !step || *maximum <= 0.0 || *step <= 0.0) {
            setEnabled(false);
            return false;
        }

        // Prices per joule are very small. Four decimal places would display an
        // ordinary nonzero tariff as zero (or "Not priced"). Keep sufficient
        // input resolution for the 0.01 currency/kWh editing step, irrespective
        // of the selected energy denominator.
        int decimals = 4;
        if (*step < 1.0)
            decimals = std::max(decimals, static_cast<int>(std::ceil(-std::log10(*step))) + 3);
        const std::optional<double> actual = UnitEnergyPriceBindingStorage::binding_.presentation(unit_id);
        if (actual && *actual > 0.0 && *actual < 1.0)
            decimals = std::max(decimals, static_cast<int>(std::ceil(-std::log10(*actual))) + 2);
        const NumericFormatPolicy policy = context.formatPolicy(AOWIS_QSTRING_QUANTITY("energy"));
        if (policy.notation == NumericFormatPolicy::Notation::Fixed)
            decimals = std::max(decimals, policy.precision);
        decimals = std::clamp(decimals, 4, 18);

        const QString label = currency.isEmpty()
            ? QStringLiteral("/") + unit
            : currency + QStringLiteral("/") + unit;
        return refresh({unit_id, NumericText::suffix(label), 0.0, *maximum,
                        *step, decimals, policy.grouping, context.locale()});
    }
};
