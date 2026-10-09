#ifndef EPANET_JS_PROJECT_IDENTITY_H
#define EPANET_JS_PROJECT_IDENTITY_H

#include "import/epanet_js_project_reader.h"
#include <QByteArray>
#include <QJsonObject>
#include <QStringList>
#include <QUuid>

namespace EpanetJsProjectIdentity
{
QStringList entityTables();
QStringList nodeTables();
QStringList linkTables();
QUuid aowisEpanetJsNamespace();
QJsonObject projectSettingsObject(const EpanetJsProjectSnapshot &project);
QByteArray fallbackProjectIdentitySeed(const EpanetJsProjectSnapshot &project);
}
#endif // EPANET_JS_PROJECT_IDENTITY_H
