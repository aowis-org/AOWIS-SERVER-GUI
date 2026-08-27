#include "gui_configuration.h"

#include <QByteArray>
#include <QDebug>
#include <QKeyEvent>
#include <QString>

#ifndef AOWIS_HAS_QRHI
#define AOWIS_HAS_QRHI 0
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QtGlobal>
#endif

namespace
{
QKeySequence parseShortcutKeySequence(const QString &shortcut)
{
    QString portable_text = shortcut.trimmed();
    if (portable_text.startsWith(QStringLiteral("Win+"), Qt::CaseInsensitive))
        portable_text.replace(0, 4, QStringLiteral("Meta+"));

    return QKeySequence::fromString(portable_text, QKeySequence::PortableText);
}

#ifndef __EMSCRIPTEN__
void ensureSettingDefault(QSettings &settings, const QString &key, const QString &value)
{
    if (!settings.contains(key))
        settings.setValue(key, value);
}

QString loadShortcutSetting(QSettings &settings, const QString &key, const QString &default_value)
{
    const QString value = settings.value(key, default_value).toString().trimmed();
    if (value.isEmpty())
        return QString();
    if (!parseShortcutKeySequence(value).isEmpty())
        return value;

    qWarning() << "Invalid shortcut" << key << "=" << value
               << "- using advertised default" << default_value;
    return default_value;
}
#endif

#ifdef __EMSCRIPTEN__
EM_JS(int, aowisBuiltinExamplesEnabled, (),
{
    const configuration = globalThis.aowisGuiConfiguration || {};
    return configuration.examplesBuiltinEnable === false ? 0 : 1;
});

EM_JS(int, aowisWasmRhiRendererRequested, (),
{
    const configuration = globalThis.aowisGuiConfiguration || {};
    const renderer = typeof configuration.mapWasmRenderer === "string"
        ? configuration.mapWasmRenderer.trim().toLowerCase()
        : "rhi";
    return renderer === "browser" ? 0 : 1;
});

GuiConfiguration loadConfiguration()
{
    GuiConfiguration configuration;
    configuration.examples_builtin_enable = aowisBuiltinExamplesEnabled() != 0;
    configuration.map_wasm_renderer = aowisWasmRhiRendererRequested() != 0
        ? WasmMapRenderer::Rhi
        : WasmMapRenderer::Browser;
    qInfo() << "Loaded GUI configuration from webroot/aowis-server-gui.ini: examples_builtin_enable ="
            << configuration.examples_builtin_enable
            << "map_wasm_renderer =" << wasmMapRendererName(configuration.map_wasm_renderer);
    return configuration;
}
#else
#ifdef Q_OS_WIN
QString legacyNativeConfigurationPath()
{
    QString data_directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (data_directory.isEmpty())
        data_directory = QDir::home().filePath(QStringLiteral("AppData/Local"));

    return QDir(data_directory).filePath(
        QStringLiteral("aowis-server-gui/aowis-server-gui.ini"));
}
#endif

QString nativeConfigurationPath()
{
#ifdef Q_OS_WIN
    QString data_directory = qEnvironmentVariable("APPDATA");
    if (data_directory.isEmpty())
        data_directory = QDir::home().filePath(QStringLiteral("AppData/Roaming"));
#else
    QString data_directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (data_directory.isEmpty())
        data_directory = QDir::home().filePath(QStringLiteral(".local/share"));
#endif

    return QDir(data_directory).filePath(
        QStringLiteral("aowis-server-gui/aowis-server-gui.ini"));
}

bool createDefaultConfiguration(const QString &path)
{
    const QFileInfo file_info(path);
    if (!QDir().mkpath(file_info.absolutePath()))
    {
        qWarning() << "Failed to create GUI configuration directory:"
                   << file_info.absolutePath();
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qWarning() << "Failed to create GUI configuration:" << path
                   << file.errorString();
        return false;
    }

    static const QByteArray contents(
        "; AOWIS Server GUI configuration\n"
        "; Restart the application after changing this file.\n"
        "\n"
        "[gui]\n"
        "examples_builtin_enable=true\n"
        "map_desktop_renderer=rhi\n"
        "\n"
        "[shortcuts]\n"
        "sidebar_toggle=Win+Tab\n"
        "fullscreen=F11\n"
        "map_monitor_fullscreen=Ctrl+F11\n"
        "simulation_run=Ctrl+R\n"
        "simulation_run_alternate=Shift+Enter\n"
        "map_zoom_in=E\n"
        "map_zoom_out=Q\n"
        "map_pan_up=W\n"
        "map_pan_down=S\n"
        "map_pan_left=A\n"
        "map_pan_right=D\n"
        "map_provider_arcgis_sat=F1\n"
        "map_provider_openstreetmap=F2\n"
        "map_provider_opentopomap=F3\n"
        "map_provider_cycloosm=F4\n"
        "map_editor_select=Esc\n"
        "map_editor_delete=Del\n"
        "map_editor_add_pipe=1\n"
        "map_editor_add_junction=2\n"
        "map_editor_add_valve=3\n"
        "map_editor_add_customer_point=4\n"
        "map_editor_add_pump=5\n"
        "map_editor_add_tank=6\n"
        "map_editor_add_power_source=7\n"
        "map_editor_add_reservoir=8\n"
        "map_editor_add_note=9\n"
        "\n"
        "[map_server]\n"
        "base_url=http://aowis-server-map.localhost:80\n"
        "api_key=\n"
        "delete_api_key=\n");

    if (file.write(contents) != contents.size())
    {
        qWarning() << "Failed to write GUI configuration:" << path
                   << file.errorString();
        file.cancelWriting();
        return false;
    }

    if (!file.commit())
    {
        qWarning() << "Failed to commit GUI configuration:" << path
                   << file.errorString();
        return false;
    }

    qInfo() << "Created default GUI configuration:" << path;
    return true;
}

DesktopMapRenderer parseDesktopMapRenderer(const QString &value, bool *valid)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("cpu"))
    {
        if (valid != nullptr)
            *valid = true;
        return DesktopMapRenderer::Cpu;
    }

    if (normalized == QStringLiteral("rhi"))
    {
        if (valid != nullptr)
            *valid = true;
        return DesktopMapRenderer::Rhi;
    }

    if (valid != nullptr)
        *valid = false;
    return DesktopMapRenderer::Rhi;
}

GuiConfiguration loadConfiguration()
{
    const QString path = nativeConfigurationPath();
    if (!QFile::exists(path))
    {
#ifdef Q_OS_WIN
        const QString legacy_path = legacyNativeConfigurationPath();
        if (legacy_path != path && QFile::exists(legacy_path))
        {
            const QFileInfo file_info(path);
            if (QDir().mkpath(file_info.absolutePath()) && QFile::copy(legacy_path, path))
                qInfo() << "Migrated GUI configuration from" << legacy_path << "to" << path;
            else
                qWarning() << "Failed to migrate GUI configuration from" << legacy_path
                           << "to" << path;
        }
#endif

        if (!QFile::exists(path))
            createDefaultConfiguration(path);
    }

    QSettings settings(path, QSettings::IniFormat);

    const GuiShortcutConfiguration advertised_shortcuts;
    ensureSettingDefault(settings, QStringLiteral("gui/map_desktop_renderer"), QStringLiteral("rhi"));
    ensureSettingDefault(settings, QStringLiteral("shortcuts/sidebar_toggle"), advertised_shortcuts.sidebar_toggle);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/fullscreen"), advertised_shortcuts.fullscreen);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_monitor_fullscreen"), advertised_shortcuts.map_monitor_fullscreen);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/simulation_run"), advertised_shortcuts.simulation_run);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/simulation_run_alternate"), advertised_shortcuts.simulation_run_alternate);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_zoom_in"), advertised_shortcuts.map_zoom_in);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_zoom_out"), advertised_shortcuts.map_zoom_out);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_pan_up"), advertised_shortcuts.map_pan_up);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_pan_down"), advertised_shortcuts.map_pan_down);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_pan_left"), advertised_shortcuts.map_pan_left);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_pan_right"), advertised_shortcuts.map_pan_right);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_provider_arcgis_sat"), advertised_shortcuts.map_provider_arcgis_sat);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_provider_openstreetmap"), advertised_shortcuts.map_provider_openstreetmap);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_provider_opentopomap"), advertised_shortcuts.map_provider_opentopomap);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_provider_cycloosm"), advertised_shortcuts.map_provider_cycloosm);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_editor_select"), advertised_shortcuts.map_editor_select);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_editor_delete"), advertised_shortcuts.map_editor_delete);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_editor_add_pipe"), advertised_shortcuts.map_editor_add_pipe);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_editor_add_junction"), advertised_shortcuts.map_editor_add_junction);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_editor_add_valve"), advertised_shortcuts.map_editor_add_valve);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_editor_add_customer_point"), advertised_shortcuts.map_editor_add_customer_point);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_editor_add_pump"), advertised_shortcuts.map_editor_add_pump);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_editor_add_tank"), advertised_shortcuts.map_editor_add_tank);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_editor_add_power_source"), advertised_shortcuts.map_editor_add_power_source);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_editor_add_reservoir"), advertised_shortcuts.map_editor_add_reservoir);
    ensureSettingDefault(settings, QStringLiteral("shortcuts/map_editor_add_note"), advertised_shortcuts.map_editor_add_note);
    settings.sync();

    GuiConfiguration configuration;
    configuration.examples_builtin_enable = settings.value(
        QStringLiteral("gui/examples_builtin_enable"), true).toBool();

    const QString configured_renderer = settings.value(
        QStringLiteral("gui/map_desktop_renderer"), QStringLiteral("rhi")).toString();
    bool renderer_valid = false;
    configuration.map_desktop_renderer = parseDesktopMapRenderer(
        configured_renderer, &renderer_valid);
    if (!renderer_valid)
    {
        qWarning() << "Invalid gui/map_desktop_renderer value" << configured_renderer
                   << "in" << path << "- using rhi. Valid values are: cpu, rhi.";
    }

    configuration.shortcuts.sidebar_toggle = loadShortcutSetting(settings, QStringLiteral("shortcuts/sidebar_toggle"), advertised_shortcuts.sidebar_toggle);
    configuration.shortcuts.fullscreen = loadShortcutSetting(settings, QStringLiteral("shortcuts/fullscreen"), advertised_shortcuts.fullscreen);
    configuration.shortcuts.map_monitor_fullscreen = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_monitor_fullscreen"), advertised_shortcuts.map_monitor_fullscreen);
    configuration.shortcuts.simulation_run = loadShortcutSetting(settings, QStringLiteral("shortcuts/simulation_run"), advertised_shortcuts.simulation_run);
    configuration.shortcuts.simulation_run_alternate = loadShortcutSetting(settings, QStringLiteral("shortcuts/simulation_run_alternate"), advertised_shortcuts.simulation_run_alternate);
    configuration.shortcuts.map_zoom_in = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_zoom_in"), advertised_shortcuts.map_zoom_in);
    configuration.shortcuts.map_zoom_out = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_zoom_out"), advertised_shortcuts.map_zoom_out);
    configuration.shortcuts.map_pan_up = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_pan_up"), advertised_shortcuts.map_pan_up);
    configuration.shortcuts.map_pan_down = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_pan_down"), advertised_shortcuts.map_pan_down);
    configuration.shortcuts.map_pan_left = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_pan_left"), advertised_shortcuts.map_pan_left);
    configuration.shortcuts.map_pan_right = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_pan_right"), advertised_shortcuts.map_pan_right);
    configuration.shortcuts.map_provider_arcgis_sat = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_provider_arcgis_sat"), advertised_shortcuts.map_provider_arcgis_sat);
    configuration.shortcuts.map_provider_openstreetmap = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_provider_openstreetmap"), advertised_shortcuts.map_provider_openstreetmap);
    configuration.shortcuts.map_provider_opentopomap = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_provider_opentopomap"), advertised_shortcuts.map_provider_opentopomap);
    configuration.shortcuts.map_provider_cycloosm = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_provider_cycloosm"), advertised_shortcuts.map_provider_cycloosm);
    configuration.shortcuts.map_editor_select = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_editor_select"), advertised_shortcuts.map_editor_select);
    configuration.shortcuts.map_editor_delete = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_editor_delete"), advertised_shortcuts.map_editor_delete);
    configuration.shortcuts.map_editor_add_pipe = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_editor_add_pipe"), advertised_shortcuts.map_editor_add_pipe);
    configuration.shortcuts.map_editor_add_junction = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_editor_add_junction"), advertised_shortcuts.map_editor_add_junction);
    configuration.shortcuts.map_editor_add_valve = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_editor_add_valve"), advertised_shortcuts.map_editor_add_valve);
    configuration.shortcuts.map_editor_add_customer_point = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_editor_add_customer_point"), advertised_shortcuts.map_editor_add_customer_point);
    configuration.shortcuts.map_editor_add_pump = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_editor_add_pump"), advertised_shortcuts.map_editor_add_pump);
    configuration.shortcuts.map_editor_add_tank = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_editor_add_tank"), advertised_shortcuts.map_editor_add_tank);
    configuration.shortcuts.map_editor_add_power_source = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_editor_add_power_source"), advertised_shortcuts.map_editor_add_power_source);
    configuration.shortcuts.map_editor_add_reservoir = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_editor_add_reservoir"), advertised_shortcuts.map_editor_add_reservoir);
    configuration.shortcuts.map_editor_add_note = loadShortcutSetting(settings, QStringLiteral("shortcuts/map_editor_add_note"), advertised_shortcuts.map_editor_add_note);

    if (settings.status() != QSettings::NoError)
        qWarning() << "Failed to read GUI configuration:" << path;
    else
        qInfo() << "Loaded GUI configuration:" << path
                << "examples_builtin_enable =" << configuration.examples_builtin_enable
                << "map_desktop_renderer ="
                << desktopMapRendererName(configuration.map_desktop_renderer)
                << "rhi_build_available =" << desktopMapRhiBuildAvailable();

    return configuration;
}
#endif
}

QKeySequence guiShortcutKeySequence(const QString &shortcut)
{
    return parseShortcutKeySequence(shortcut);
}

bool guiShortcutMatches(const QKeyEvent *event, const QString &shortcut,
                        Qt::KeyboardModifiers allowed_extra_modifiers)
{
    if (event == nullptr)
        return false;

    const QKeySequence sequence = parseShortcutKeySequence(shortcut);
    if (sequence.count() != 1)
        return false;

    const QKeyCombination combination = sequence[0];
    if (event->key() != combination.key())
        return false;

    const Qt::KeyboardModifiers expected_modifiers = combination.keyboardModifiers();
    const Qt::KeyboardModifiers actual_modifiers = event->modifiers();
    if ((actual_modifiers & expected_modifiers) != expected_modifiers)
        return false;

    return (actual_modifiers & ~(expected_modifiers | allowed_extra_modifiers)) == Qt::NoModifier;
}

QString guiConfigurationFilePath()
{
#ifdef __EMSCRIPTEN__
    return QStringLiteral("webroot/aowis-server-gui.ini");
#else
    return nativeConfigurationPath();
#endif
}

const GuiConfiguration &guiConfiguration()
{
    static const GuiConfiguration configuration = loadConfiguration();
    return configuration;
}

const char *desktopMapRendererName(DesktopMapRenderer renderer)
{
    switch (renderer)
    {
        case DesktopMapRenderer::Rhi:
            return "rhi";
        case DesktopMapRenderer::Cpu:
        default:
            return "cpu";
    }
}

bool desktopMapRhiBuildAvailable()
{
    return AOWIS_HAS_QRHI != 0;
}

DesktopMapRenderer desktopMapRenderer()
{
#ifdef __EMSCRIPTEN__
    return DesktopMapRenderer::Rhi;
#else
    static const DesktopMapRenderer renderer = []()
    {
        const DesktopMapRenderer requested = guiConfiguration().map_desktop_renderer;
        if (requested == DesktopMapRenderer::Rhi && !desktopMapRhiBuildAvailable())
        {
            qWarning() << "Desktop map renderer 'rhi' was requested, but this build has no "
                          "QRhi support. Falling back to the CPU renderer.";
            return DesktopMapRenderer::Cpu;
        }

        return requested;
    }();
    return renderer;
#endif
}

const char *wasmMapRendererName(WasmMapRenderer renderer)
{
    switch (renderer)
    {
        case WasmMapRenderer::Browser:
            return "browser";
        case WasmMapRenderer::Rhi:
        default:
            return "rhi";
    }
}

bool wasmMapRhiBuildAvailable()
{
#ifdef __EMSCRIPTEN__
    return AOWIS_HAS_QRHI != 0;
#else
    return false;
#endif
}

WasmMapRenderer wasmMapRenderer()
{
#ifdef __EMSCRIPTEN__
    static const WasmMapRenderer renderer = []()
    {
        const WasmMapRenderer requested = guiConfiguration().map_wasm_renderer;
        if (requested == WasmMapRenderer::Rhi && !wasmMapRhiBuildAvailable())
        {
            qWarning() << "WASM map renderer 'rhi' was requested, but this build has no "
                          "QRhi support. Falling back to the browser renderer.";
            return WasmMapRenderer::Browser;
        }
        return requested;
    }();
    return renderer;
#else
    return WasmMapRenderer::Browser;
#endif
}
