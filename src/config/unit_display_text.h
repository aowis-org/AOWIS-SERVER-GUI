#pragma once

#include <QString>
#include <string_view>

// The Model's unit identifiers are intentionally ASCII and must remain intact
// in conversions, profile data and external formats. Only human-facing labels
// use the typography below. This formatter also accepts already-formatted text.
namespace UnitDisplayText
{
[[nodiscard]] inline QString symbol(QString identifier)
{
    if (identifier == QStringLiteral("deg")) return QStringLiteral("°");
    if (identifier == QStringLiteral("millift")) return QStringLiteral("0.001 ft");

    // Compound UCUM identifiers first; subsequent substitutions handle powers.
    identifier.replace(QStringLiteral("kW.h"), QStringLiteral("kWh"));
    identifier.replace(QStringLiteral("acre.ft"), QStringLiteral("acre·ft"));
    identifier.replace(QStringLiteral("MUSgal"), QStringLiteral("10⁶ US gal"));
    identifier.replace(QStringLiteral("MImpgal"), QStringLiteral("10⁶ Imp gal"));
    identifier.replace(QStringLiteral("100.m"), QStringLiteral("100 m"));
    identifier.replace(QStringLiteral("100.ft"), QStringLiteral("100 ft"));
    identifier.replace(QStringLiteral("1000ft"), QStringLiteral("1000 ft"));
    identifier.replace(QStringLiteral("mm2"), QStringLiteral("mm²"));
    identifier.replace(QStringLiteral("cm2"), QStringLiteral("cm²"));
    identifier.replace(QStringLiteral("in2"), QStringLiteral("in²"));
    identifier.replace(QStringLiteral("ft2"), QStringLiteral("ft²"));
    identifier.replace(QStringLiteral("m2"), QStringLiteral("m²"));
    identifier.replace(QStringLiteral("mm3"), QStringLiteral("mm³"));
    identifier.replace(QStringLiteral("cm3"), QStringLiteral("cm³"));
    identifier.replace(QStringLiteral("in3"), QStringLiteral("in³"));
    identifier.replace(QStringLiteral("ft3"), QStringLiteral("ft³"));
    identifier.replace(QStringLiteral("m3"), QStringLiteral("m³"));
    identifier.replace(QStringLiteral("ug"), QStringLiteral("µg"));
    identifier.replace(QStringLiteral("uF"), QStringLiteral("µF"));
    identifier.replace(QStringLiteral("kOhm"), QStringLiteral("kΩ"));
    identifier.replace(QStringLiteral("Ohm"), QStringLiteral("Ω"));
    // Typeset an integer exponent only when the entire exponent is known;
    // never turn a fractional exponent such as ^2.5 into a misleading ².5.
    const QString superscript_digits = QStringLiteral("⁰¹²³⁴⁵⁶⁷⁸⁹");
    qsizetype caret = identifier.indexOf(QLatin1Char('^'));
    while (caret >= 0)
    {
        qsizetype end = caret + 1;
        QString exponent;
        if (end < identifier.size() && identifier.at(end) == QLatin1Char('-'))
        {
            exponent += QChar(0x207b);
            ++end;
        }
        const qsizetype digits_start = end;
        while (end < identifier.size()
               && identifier.at(end).unicode() >= '0'
               && identifier.at(end).unicode() <= '9')
        {
            exponent += superscript_digits.at(identifier.at(end).unicode() - '0');
            ++end;
        }
        if (end > digits_start && (end == identifier.size()
                                   || identifier.at(end) == QLatin1Char('/')))
        {
            identifier.replace(caret, end - caret, exponent);
            caret = identifier.indexOf(QLatin1Char('^'), caret + exponent.size());
        }
        else
            caret = identifier.indexOf(QLatin1Char('^'), caret + 1);
    }
    return identifier;
}

[[nodiscard]] inline QString quantity(std::string_view key)
{
    QString label = QString::fromLatin1(key.data(), static_cast<qsizetype>(key.size()));
    label.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!label.isEmpty()) label[0] = label.at(0).toUpper();
    label.replace(QStringLiteral("Darcy weisbach"), QStringLiteral("Darcy–Weisbach"));
    label.replace(QStringLiteral("Hazen williams"), QStringLiteral("Hazen–Williams"));
    label.replace(QStringLiteral("Chezy manning"), QStringLiteral("Chézy–Manning"));
    if (label == QStringLiteral("Pressure head")) return QStringLiteral("Pressure / pressure head");
    label.replace(QStringLiteral("First order"), QStringLiteral("First-order"));
    label.replace(QStringLiteral("Zero order"), QStringLiteral("Zero-order"));
    label.replace(QStringLiteral("100m"), QStringLiteral("100 m"));
    return label;
}
} // namespace UnitDisplayText
