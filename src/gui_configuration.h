#ifndef GUI_CONFIGURATION_H
#define GUI_CONFIGURATION_H

#include <QKeySequence>
#include <QString>

class QKeyEvent;

enum class DesktopMapRenderer
{
    Cpu,
    Rhi
};

struct GuiShortcutConfiguration
{
    QString sidebar_toggle = QStringLiteral("Win+Tab");
    QString fullscreen = QStringLiteral("F11");
    QString simulation_run = QStringLiteral("Ctrl+R");
    QString simulation_run_alternate = QStringLiteral("Shift+Enter");

    QString map_zoom_in = QStringLiteral("E");
    QString map_zoom_out = QStringLiteral("Q");
    QString map_pan_up = QStringLiteral("W");
    QString map_pan_down = QStringLiteral("S");
    QString map_pan_left = QStringLiteral("A");
    QString map_pan_right = QStringLiteral("D");
    QString map_provider_arcgis_sat = QStringLiteral("F1");
    QString map_provider_openstreetmap = QStringLiteral("F2");
    QString map_provider_opentopomap = QStringLiteral("F3");
    QString map_provider_cycloosm = QStringLiteral("F4");

    QString map_editor_select = QStringLiteral("Esc");
    QString map_editor_delete = QStringLiteral("Del");
    QString map_editor_add_pipe = QStringLiteral("1");
    QString map_editor_add_junction = QStringLiteral("2");
    QString map_editor_add_valve = QStringLiteral("3");
    QString map_editor_add_customer_point = QStringLiteral("4");
    QString map_editor_add_pump = QStringLiteral("5");
    QString map_editor_add_tank = QStringLiteral("6");
    QString map_editor_add_power_source = QStringLiteral("7");
    QString map_editor_add_reservoir = QStringLiteral("8");
    QString map_editor_add_note = QStringLiteral("9");
};

struct GuiConfiguration
{
    bool examples_builtin_enable = true;
    DesktopMapRenderer map_desktop_renderer = DesktopMapRenderer::Cpu;
    GuiShortcutConfiguration shortcuts;
};

QString guiConfigurationFilePath();
const GuiConfiguration &guiConfiguration();
QKeySequence guiShortcutKeySequence(const QString &shortcut);
bool guiShortcutMatches(const QKeyEvent *event, const QString &shortcut,
                        Qt::KeyboardModifiers allowed_extra_modifiers = Qt::NoModifier);
DesktopMapRenderer desktopMapRenderer();
bool desktopMapRhiBuildAvailable();
const char *desktopMapRendererName(DesktopMapRenderer renderer);

#endif // GUI_CONFIGURATION_H
