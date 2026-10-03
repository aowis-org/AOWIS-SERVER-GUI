#ifndef AOWIS_EPANET_JS_HYDRAULIC_EQUIVALENCE_H
#define AOWIS_EPANET_JS_HYDRAULIC_EQUIVALENCE_H

#include <QtGlobal>

#include <QString>

#include <aowis/model/hydraulic/network_hydraulic.h>

struct EpanetJsHydraulicEquivalenceResult
{
    bool success = false;
    QString error;
    qsizetype timesteps_compared = 0;
    qsizetype hydraulic_values_compared = 0;
};

struct EpanetJsModelInputEquivalenceResult
{
    bool success = false;
    QString error;
    qsizetype sections_compared = 0;
    qsizetype lines_compared = 0;
};

EpanetJsModelInputEquivalenceResult compareEpanetJsModelInputs(
    const QString &reference_inp_path,
    const NetworkHydraulic &converted_network);

EpanetJsHydraulicEquivalenceResult compareEpanetJsHydraulics(
    const QString &reference_inp_path,
    const NetworkHydraulic &converted_network);

#endif // AOWIS_EPANET_JS_HYDRAULIC_EQUIVALENCE_H
