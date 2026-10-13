#pragma once

#include "config/unit_profile_manager.h"
#ifdef Q_OS_WASM
#include "widgets/wasm_popup_menu.h"
#else
#include <QAction>
#include <QMenu>
#endif
#include <QObject>

#include <cstddef>
#include <functional>
#include <string_view>

// Shared layout for the toolbar, Settings selector and custom-profile source menu.
// Resolve built-ins by stable ID rather than by their position in the collection.
namespace UnitProfileMenu {
#ifdef Q_OS_WASM
using Menu = WasmPopupMenu;
#else
using Menu = QMenu;
#endif

enum class Group { Canonical, FeaturedMetric, OtherMetric, Imperial, Custom };

[[nodiscard]] inline Group group(const UnitProfileManager::Profile &profile)
{
    if (!profile.builtin) return Group::Custom;
    if (profile.id == QStringLiteral("canonical")) return Group::Canonical;
    for (std::size_t i = 0; i < aowis::units::flow_template_names.size(); ++i) {
        if (profile.id != QString::fromLatin1(aowis::units::flow_template_names[i].data(),
                                              static_cast<qsizetype>(aowis::units::flow_template_names[i].size())))
            continue;
        if (i < 2) return Group::FeaturedMetric;
        return i < 6 ? Group::OtherMetric : Group::Imperial;
    }
    // Unknown built-in presets remain accessible rather than being hidden.
    return Group::OtherMetric;
}

[[nodiscard]] inline QString label(const UnitProfileManager::Profile &profile)
{
    if (!profile.builtin) return profile.name;
    if (profile.id == QStringLiteral("canonical"))
        return QStringLiteral("AOWIS Canonical — standard AOWIS units");
    if (profile.id == QStringLiteral("CMH"))
        return QStringLiteral("CMH — cubic meters per hour");
    if (profile.id == QStringLiteral("LPS"))
        return QStringLiteral("LPS — liters per second");
    if (profile.id == QStringLiteral("LPM"))
        return QStringLiteral("LPM — liters per minute");
    if (profile.id == QStringLiteral("MLD"))
        return QStringLiteral("MLD — million liters per day");
    if (profile.id == QStringLiteral("CMD"))
        return QStringLiteral("CMD — cubic meters per day");
    if (profile.id == QStringLiteral("CMS"))
        return QStringLiteral("CMS — cubic meters per second");
    if (profile.id == QStringLiteral("CFS"))
        return QStringLiteral("CFS — cubic feet per second");
    if (profile.id == QStringLiteral("GPM"))
        return QStringLiteral("GPM — US gallons per minute");
    if (profile.id == QStringLiteral("MGD"))
        return QStringLiteral("MGD — million US gallons per day");
    if (profile.id == QStringLiteral("IMGD"))
        return QStringLiteral("IMGD — million Imperial gallons per day");
    if (profile.id == QStringLiteral("AFD"))
        return QStringLiteral("AFD — acre-feet per day");
    // Preserve names of future built-in profiles until they have a description.
    return profile.name;
}

[[nodiscard]] inline Menu *subMenu(Menu *parent, const QString &title)
{
#ifdef Q_OS_WASM
    return parent->addSubMenu(title);
#else
    return parent->addMenu(title);
#endif
}

inline void populate(Menu *menu, QObject *owner,
                     const QVector<UnitProfileManager::Profile> &profiles,
                     int selectedIndex,
                     const std::function<void(int)> &onSelect,
                     const std::function<void()> &onManage = {})
{
#ifdef Q_OS_WASM
    (void)owner;
    (void)selectedIndex;
#endif
    menu->clear();
    const std::function<void(Group, Menu *)> addGroup =
#ifdef Q_OS_WASM
        [&profiles, &onSelect](Group wanted, Menu *target) {
#else
        [owner, &profiles, selectedIndex, &onSelect](Group wanted, Menu *target) {
#endif
            for (int index = 0; index < profiles.size(); ++index) {
                const UnitProfileManager::Profile &profile = profiles.at(index);
                if (group(profile) != wanted) continue;
#ifdef Q_OS_WASM
                target->addAction(label(profile), [onSelect, index] { onSelect(index); });
#else
                QAction *action = target->addAction(label(profile));
                action->setData(index);
                if (selectedIndex >= 0) {
                    action->setCheckable(true);
                    action->setChecked(index == selectedIndex);
                }
                QObject::connect(action, &QAction::triggered, owner,
                                 [onSelect, index] { onSelect(index); });
#endif
            }
        };

    addGroup(Group::Canonical, menu);
    menu->addSeparator();
    addGroup(Group::FeaturedMetric, menu);
    addGroup(Group::OtherMetric, subMenu(menu, QStringLiteral("Other metric")));
    addGroup(Group::Imperial, subMenu(menu, QStringLiteral("Imperial / US")));
#ifndef Q_OS_WASM
    menu->addSeparator();
#endif
    Menu *custom = subMenu(menu, QStringLiteral("Custom"));
    addGroup(Group::Custom, custom);
    if (onManage) {
        custom->addSeparator();
#ifdef Q_OS_WASM
        custom->addAction(QStringLiteral("Manage Units…"), onManage);
#else
        QAction *action = custom->addAction(QStringLiteral("Manage Units…"));
        QObject::connect(action, &QAction::triggered, owner, onManage);
#endif
    }
}
} // namespace UnitProfileMenu
