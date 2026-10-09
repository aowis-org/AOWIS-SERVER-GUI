#ifndef EPANET_JS_UNITS_H
#define EPANET_JS_UNITS_H

#include <QString>
#include <optional>

namespace EpanetJsUnits
{
QString normalizedUnit(QString unit);
std::optional<double> flowToM3PerH(double value, const QString &unit);
std::optional<double> lengthToM(double value, const QString &unit);
std::optional<double> volumeToM3(double value, const QString &unit);
std::optional<double> diameterToMm(double value, const QString &unit);
bool flowUnitUsesMetricLength(const QString &unit);
bool flowUnitUsesFootLength(const QString &unit);
std::optional<double> darcyRoughnessToMm(
    double value,
    const QString &roughness_unit,
    const QString &flow_unit);
std::optional<double> chemicalConcentrationScaleToMgPerL(const QString &unit);
// Converts a water-age value to canonical hours. An empty unit means
// EPANET's native hours.
std::optional<double> waterAgeToHours(double value, const QString &unit);
std::optional<double> pressureToHeadM(
    double value,
    const QString &unit,
    double specific_gravity);
// The emitter pressure basis follows the EPANET unit system implied by
// flow_unit (psi for US, metres of head for SI), not the pressure option.
std::optional<double> emitterCoefficientToCanonical(
    double source_coefficient,
    double pressure_exponent,
    const QString &flow_unit,
    double specific_gravity);
}

#endif // EPANET_JS_UNITS_H
