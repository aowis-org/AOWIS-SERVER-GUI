#pragma once

#include <aowis/model/units/unit_profile.h>
#include <QString>
#include <vector>
#include <utility>

// Persistence boundary. No QSettings, JSON, SQLite or HTTP in the Model.
struct UnitProfileSnapshot {
    std::vector<aowis::units::UnitProfile> custom_profiles;
    QString active_id;
    QString legacy_active_name;
};

class UnitProfileStorage {
public:
    virtual ~UnitProfileStorage() = default;
    [[nodiscard]] virtual UnitProfileSnapshot load() const = 0;
    virtual bool save(const UnitProfileSnapshot &snapshot) const = 0;
};

// Temporary legacy adapter; future database/API adapters implement the same interface.
class QSettingsUnitProfileStorage final : public UnitProfileStorage {
public:
    explicit QSettingsUnitProfileStorage(QString path) : path_(std::move(path)) {}
    [[nodiscard]] UnitProfileSnapshot load() const override;
    bool save(const UnitProfileSnapshot &snapshot) const override;
private:
    QString path_;
};
