#pragma once

#include <QObject>
#include <QKeySequence>
#include <QString>
#include <QVector>

class QWidget;

enum class GuiShortcutId
{
    SidebarToggle,
    Fullscreen,
    SimulationRun,
    SimulationRunAlternate,
    MapZoomIn,
    MapZoomOut,
    MapPanUp,
    MapPanDown,
    MapPanLeft,
    MapPanRight,
    MapProviderArcGisSat,
    MapProviderOpenStreetMap,
    MapProviderOpenTopoMap,
    MapProviderCycloOsm,
    MapEditorSelect,
    MapEditorDelete,
    MapEditorAddPipe,
    MapEditorAddJunction,
    MapEditorAddValve,
    MapEditorAddCustomerPoint,
    MapEditorAddPump,
    MapEditorAddTank,
    MapEditorAddPowerSource,
    MapEditorAddReservoir,
    MapEditorAddNote
};

struct GuiShortcutDefinition
{
    GuiShortcutId id;
    QString config_key;
    QString display_name;
    QString category;
    QString scope;
    QString default_shortcut;
};

class GuiShortcutRegistry : public QObject
{
    Q_OBJECT

public:
    explicit GuiShortcutRegistry(QObject *parent = nullptr);

    const QVector<GuiShortcutDefinition> &definitions() const;
    const GuiShortcutDefinition *definition(GuiShortcutId id) const;
    QString shortcut(GuiShortcutId id) const;
    QKeySequence keySequence(GuiShortcutId id) const;
    QString conflictDisplayName(GuiShortcutId id, const QKeySequence &sequence) const;

    bool setShortcut(GuiShortcutId id, const QKeySequence &sequence, QString *error_message = nullptr);
    bool resetShortcut(GuiShortcutId id, QString *error_message = nullptr);
    void resetAllShortcuts();
    void requestEdit(GuiShortcutId id);
    bool shortcutCaptureActive() const;
    void setShortcutCaptureActive(bool active);

signals:
    void shortcutChanged(GuiShortcutId id);
    void shortcutsReset();
    void editShortcutRequested(GuiShortcutId id);
    void shortcutCaptureActiveChanged(bool active);

private:
    QVector<GuiShortcutDefinition> shortcut_definitions;
    QVector<QString> shortcut_values;
    bool shortcut_capture_active = false;

    int indexForId(GuiShortcutId id) const;
    bool persistShortcut(const GuiShortcutDefinition &definition, const QString &value,
                         QString *error_message);
};

GuiShortcutRegistry &guiShortcutRegistry();
QString guiShortcutDisplayText(const QKeySequence &sequence);
QString guiShortcutPresentation(GuiShortcutId id);
void installShortcutEditContextMenu(QWidget *widget, GuiShortcutId id);
