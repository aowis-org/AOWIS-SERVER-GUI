#include "map_server_client_configuration.h"

#include "gui_configuration.h"

#include <QDebug>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

#include <cstdlib>
#else
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#endif

#ifdef __EMSCRIPTEN__
EM_JS(char *, aowisMapServerBaseUrl, (),
{
    const configuration = globalThis.aowisMapServerConfiguration || {};
    const value = typeof configuration.baseUrl === "string" ? configuration.baseUrl : "";
    return stringToNewUTF8(value);
});

EM_JS(char *, aowisMapServerApiKey, (),
{
    const configuration = globalThis.aowisMapServerConfiguration || {};
    const value = typeof configuration.apiKey === "string" ? configuration.apiKey : "";
    return stringToNewUTF8(value);
});

EM_JS(char *, aowisMapServerDeleteApiKey, (),
{
    const configuration = globalThis.aowisMapServerConfiguration || {};
    const value = typeof configuration.deleteApiKey === "string" ? configuration.deleteApiKey : "";
    return stringToNewUTF8(value);
});

#endif

namespace
{
#ifdef __EMSCRIPTEN__

QString takeWasmString(char *value)
{
    if (value == nullptr)
        return QString();

    const QString result = QString::fromUtf8(value);
    std::free(value);
    return result;
}

MapServerClientConfiguration loadConfiguration()
{
    MapServerClientConfiguration configuration;
    configuration.base_url = takeWasmString(aowisMapServerBaseUrl()).trimmed();
    configuration.api_key = takeWasmString(aowisMapServerApiKey()).trimmed().toUtf8();
    configuration.delete_api_key = takeWasmString(aowisMapServerDeleteApiKey()).trimmed().toUtf8();

    if (configuration.base_url.isEmpty())
        qCritical() << "Missing map_server/base_url in webroot/aowis-server-gui.ini";
    else
        qInfo() << "Loaded map server client configuration from webroot/aowis-server-gui.ini:"
                << configuration.base_url;
    return configuration;
}
#else
QString nativeConfigurationPath()
{
    return guiConfigurationFilePath();
}

bool createDefaultConfiguration(const QString &path)
{
    const QFileInfo file_info(path);
    if (!QDir().mkpath(file_info.absolutePath()))
    {
        qWarning() << "Failed to create map server client configuration directory:"
                   << file_info.absolutePath();
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        qWarning() << "Failed to create map server client configuration:" << path
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
        qWarning() << "Failed to write map server client configuration:" << path
                   << file.errorString();
        file.cancelWriting();
        return false;
    }

    if (!file.commit())
    {
        qWarning() << "Failed to commit map server client configuration:" << path
                   << file.errorString();
        return false;
    }

    qInfo() << "Created default map server client configuration:" << path;
    return true;
}

MapServerClientConfiguration loadConfiguration()
{
    const QString path = nativeConfigurationPath();
    if (!QFile::exists(path))
        createDefaultConfiguration(path);

    QSettings settings(path, QSettings::IniFormat);

    MapServerClientConfiguration configuration;
    configuration.base_url = settings.value(
        QStringLiteral("map_server/base_url")).toString().trimmed();
    configuration.api_key = settings.value(
        QStringLiteral("map_server/api_key")).toString().trimmed().toUtf8();
    configuration.delete_api_key = settings.value(
        QStringLiteral("map_server/delete_api_key")).toString().trimmed().toUtf8();

    if (configuration.base_url.isEmpty())
        qCritical() << "Missing map_server/base_url in map server client configuration:" << path;

    if (settings.status() != QSettings::NoError)
        qWarning() << "Failed to read map server client configuration:" << path;
    else
        qInfo() << "Loaded map server client configuration:" << path;

    return configuration;
}
#endif
}

const MapServerClientConfiguration &mapServerClientConfiguration()
{
    static const MapServerClientConfiguration configuration = loadConfiguration();
    return configuration;
}
