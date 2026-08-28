#include "builtin_examples.h"

const QList<BuiltinExampleRevision> &builtinExampleRevisions()
{
    static const QList<BuiltinExampleRevision> revisions = {
        {QStringLiteral("UK style"), QStringLiteral("01-uk-style.inp"), QStringLiteral(":/examples/epanet/01-uk-style.inp")},
        {QStringLiteral("US style"), QStringLiteral("02-us-style.inp"), QStringLiteral(":/examples/epanet/02-us-style.inp")},
        {QStringLiteral("NET3"), QStringLiteral("NET3.INP"), QStringLiteral(":/examples/epanet/NET3.INP")},
        {QStringLiteral("EXNET-3"), QStringLiteral("exnet-3.inp"), QStringLiteral(":/examples/epanet/exnet-3.inp")},
        {QStringLiteral("KY4"), QStringLiteral("ky4.inp"), QStringLiteral(":/examples/epanet/ky4.inp")},
        {QStringLiteral("Tutorial - Extended Period"), QStringLiteral("tutorial-eps.inp"), QStringLiteral(":/examples/epanet/tutorial-eps.inp")},
        {QStringLiteral("Tutorial - Leakage"), QStringLiteral("tutorial-leakage.inp"), QStringLiteral(":/examples/epanet/tutorial-leakage.inp")},
        {QStringLiteral("AOWIS 4 Tanks"), QStringLiteral("4tanks.inp"), QStringLiteral(":/examples/epanet/4tanks.inp")}
    };

    return revisions;
}
