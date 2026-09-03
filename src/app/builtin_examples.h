#ifndef BUILTIN_EXAMPLES_H
#define BUILTIN_EXAMPLES_H

#include <QList>
#include <QString>

struct BuiltinExampleRevision
{
    QString display_name;
    QString file_name;
    QString resource_path;
};

const QList<BuiltinExampleRevision> &builtinExampleRevisions();

#endif // BUILTIN_EXAMPLES_H
