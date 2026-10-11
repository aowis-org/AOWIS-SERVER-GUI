#pragma once

#include "numeric_text.h"
#include <aowis/model/units/unit_profile.h>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <functional>
#include <utility>

// Effective GUI display units, deliberately independent of project/model revisions.
// This context never converts or writes model measurements.
class UnitPresentationContext
{
public:
    using Generation = quint64;

    [[nodiscard]] Generation generation() const { return this->generation_; }
    [[nodiscard]] QString unit(const QString &quantity) const {
        const QString selected = this->effective_units_.value(quantity).toString();
        if (!selected.isEmpty()) return selected;
        // Partial/legacy profiles inherit new semantic display quantities.
        const std::string_view parent = aowis::units::inheritedUnitQuantity(
            quantity.toStdString());
        if (parent.empty()) return {};
        return this->effective_units_.value(
            QString::fromLatin1(parent.data(), static_cast<qsizetype>(parent.size()))).toString();
    }
    [[nodiscard]] const QJsonObject &effectiveUnits() const { return this->effective_units_; }

    [[nodiscard]] NumericFormatPolicy formatPolicy(const QString &quantity = {}) const {
        return this->format_overrides_.value(quantity, this->default_format_);
    }
    [[nodiscard]] QLocale locale() const { return this->locale_; }
    bool setLocale(const QLocale &locale) {
        if (locale == this->locale_) return false;
        this->locale_ = locale;
        notifyPresentationChanged();
        return true;
    }
    bool setFormatPolicy(const NumericFormatPolicy &policy, const QString &quantity = {}) {
        if (!policy.valid()) return false;
        if (quantity.isEmpty()) {
            if (policy == this->default_format_) return false;
            this->default_format_ = policy;
        } else {
            const bool changed = !(formatPolicy(quantity) == policy);
            this->format_overrides_.insert(quantity, policy);
            if (!changed) return false;
        }
        notifyPresentationChanged();
        return true;
    }
    bool clearFormatPolicy(const QString &quantity) {
        const NumericFormatPolicy previous = formatPolicy(quantity);
        if (!this->format_overrides_.remove(quantity)) return false;
        if (previous == formatPolicy(quantity)) return false;
        notifyPresentationChanged();
        return true;
    }

    // The manager initializes this once, after loading the selected profile.
    void initialize(const QJsonObject &units) {
        this->effective_units_ = units;
    }

    // Returns false if only the profile identity/name changed, not its effective units.
    bool update(const QJsonObject &units) {
        if (this->effective_units_ == units) return false;
        this->effective_units_ = units;
        notifyPresentationChanged();
        return true;
    }

    // Subscribers should mark themselves stale. Phase 3 adds visibility-aware
    // refresh at the view boundary rather than subscribing every numeric field.
    void subscribe(QObject *owner, std::function<void(Generation)> refresh) {
        if (!owner || !refresh) return;
        this->listeners_.append({owner, std::move(refresh)});
    }

private:
    void notifyPresentationChanged() {
        ++this->generation_;
        const QVector<Listener> snapshot = this->listeners_;
        for (const Listener &listener : snapshot) {
            if (listener.owner) listener.refresh(this->generation_);
        }
        for (qsizetype i = this->listeners_.size(); i > 0; --i) {
            if (!this->listeners_.at(i - 1).owner)
                this->listeners_.removeAt(i - 1);
        }
    }
    struct Listener {
        QPointer<QObject> owner;
        std::function<void(Generation)> refresh;
    };
    NumericFormatPolicy default_format_;
    QHash<QString, NumericFormatPolicy> format_overrides_;
    QLocale locale_;
    Generation generation_ = 1;
    QJsonObject effective_units_;
    QVector<Listener> listeners_;
};
