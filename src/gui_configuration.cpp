#include "gui_configuration.h"

#include <QByteArray>
#include <QDebug>
#include <QString>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
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
QString nativeConfigurationPath()
{
    QString data_directory = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (data_directory.isEmpty())
        data_directory = QDir::home().filePath(QStringLiteral(".local/share"));

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

GuiConfiguration loadConfiguration()
{
    const QString path = nativeConfigurationPath();
    if (!QFile::exists(path))
        createDefaultConfiguration(path);

    QSettings settings(path, QSettings::IniFormat);

    GuiConfiguration configuration;
    configuration.examples_builtin_enable = settings.value(
        QStringLiteral("gui/examples_builtin_enable"), true).toBool();

    if (settings.status() != QSettings::NoError)
        qWarning() << "Failed to read GUI configuration:" << path;
    else
        qInfo() << "Loaded GUI configuration:" << path
                << "examples_builtin_enable =" << configuration.examples_builtin_enable;

    return configuration;
}
#endif
}

const GuiConfiguration &guiConfiguration()
{
    static const GuiConfiguration configuration = loadConfiguration();
    return configuration;
}
