#include "gui_configuration.h"

#include <QByteArray>
#include <QDebug>
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
#ifdef __EMSCRIPTEN__
EM_JS(int, aowisBuiltinExamplesEnabled, (),
{
    const configuration = globalThis.aowisGuiConfiguration || {};
    return configuration.examplesBuiltinEnable === false ? 0 : 1;
});

GuiConfiguration loadConfiguration()
{
    GuiConfiguration configuration;
    configuration.examples_builtin_enable = aowisBuiltinExamplesEnabled() != 0;
    qInfo() << "Loaded GUI configuration from webroot/aowis-server-gui.ini: examples_builtin_enable ="
            << configuration.examples_builtin_enable;
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
        "map_desktop_renderer=cpu\n"
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
    return DesktopMapRenderer::Cpu;
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

    GuiConfiguration configuration;
    configuration.examples_builtin_enable = settings.value(
        QStringLiteral("gui/examples_builtin_enable"), true).toBool();

    const QString configured_renderer = settings.value(
        QStringLiteral("gui/map_desktop_renderer"), QStringLiteral("cpu")).toString();
    bool renderer_valid = false;
    configuration.map_desktop_renderer = parseDesktopMapRenderer(
        configured_renderer, &renderer_valid);
    if (!renderer_valid)
    {
        qWarning() << "Invalid gui/map_desktop_renderer value" << configured_renderer
                   << "in" << path << "- using cpu. Valid values are: cpu, rhi.";
    }

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
#ifdef __EMSCRIPTEN__
    return false;
#else
    return AOWIS_HAS_QRHI != 0;
#endif
}

DesktopMapRenderer desktopMapRenderer()
{
#ifdef __EMSCRIPTEN__
    return DesktopMapRenderer::Cpu;
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
