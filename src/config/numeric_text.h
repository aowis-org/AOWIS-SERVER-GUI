#pragma once

#include <QLocale>
#include <QString>
#include <cmath>
#include <optional>

// Presentation only: callers convert physical values through the Model first.
struct NumericFormatPolicy
{
    enum class Notation { Fixed, Significant, Scientific };
    Notation notation = Notation::Significant;
    int precision = 6; // Fractional digits for Fixed/Scientific; significant digits otherwise.
    bool grouping = false;
    bool normalize_negative_zero = true;

    bool valid() const {
        if (this->notation == Notation::Significant)
            return this->precision >= 1 && this->precision <= 17;
        if (this->notation == Notation::Fixed || this->notation == Notation::Scientific)
            return this->precision >= 0 && this->precision <= 323;
        return false;
    }
    bool operator==(const NumericFormatPolicy &other) const {
        return this->notation == other.notation &&
               this->precision == other.precision &&
               this->grouping == other.grouping &&
               this->normalize_negative_zero == other.normalize_negative_zero;
    }
};

namespace NumericText
{
enum class ParseStatus { Valid, Missing, Invalid, NonFinite, Overflow };
struct ParseResult
{
    ParseStatus status = ParseStatus::Invalid;
    std::optional<double> value;
};

inline QLocale localeFor(QLocale locale, bool grouping) {
    QLocale::NumberOptions options = locale.numberOptions();
    options.setFlag(QLocale::OmitGroupSeparator, !grouping);
    options.setFlag(QLocale::RejectGroupSeparator, !grouping);
    locale.setNumberOptions(options);
    return locale;
}

// Parsing never rounds, rescales percentages, infers units, or falls back to a
// different locale. The supplied symbol must match exactly when present.
inline ParseResult parse(QString text, const QLocale &locale, bool grouping = false,
                         const QString &symbol = {}) {
    text = text.trimmed();
    if (!symbol.isEmpty() && text.endsWith(symbol))
        text = text.left(text.size() - symbol.size()).trimmed();
    if (text.isEmpty()) return {ParseStatus::Missing, std::nullopt};
    const QString lower = text.toLower();
    if (lower == QStringLiteral("nan") || lower == QStringLiteral("inf") ||
        lower == QStringLiteral("+inf") || lower == QStringLiteral("-inf") ||
        lower == QStringLiteral("infinity") || lower == QStringLiteral("+infinity") ||
        lower == QStringLiteral("-infinity") || text.contains(QChar(0x221e)))
        return {ParseStatus::NonFinite, std::nullopt};
    bool ok = false;
    const double value = localeFor(locale, grouping).toDouble(text, &ok);
    if (!ok) return {std::isinf(value) ? ParseStatus::Overflow : ParseStatus::Invalid, std::nullopt};
    if (!std::isfinite(value)) return {ParseStatus::NonFinite, std::nullopt};
    return {ParseStatus::Valid, value};
}

// nullopt denotes an invalid policy. Missing/NaN/infinity have distinct display
// markers, all of which the input parser refuses as a numerical edit.
inline std::optional<QString> format(std::optional<double> value, const QLocale &locale,
                                    const NumericFormatPolicy &policy = {}) {
    if (!policy.valid()) return std::nullopt;
    if (!value) return QString(QChar(0x2014));
    if (std::isnan(*value)) return QStringLiteral("NaN");
    if (std::isinf(*value)) return (*value < 0 ? QStringLiteral("-") : QString()) + QChar(0x221e);
    const QLocale display_locale = localeFor(locale, policy.grouping);
    char notation = 'g';
    if (policy.notation == NumericFormatPolicy::Notation::Fixed) notation = 'f';
    if (policy.notation == NumericFormatPolicy::Notation::Scientific) notation = 'e';
    QString result = display_locale.toString(*value, notation, policy.precision);
    if (policy.normalize_negative_zero && std::signbit(*value)) {
        bool ok = false;
        const double represented = display_locale.toDouble(result, &ok);
        if (ok && represented == 0.0)
            result = display_locale.toString(0.0, notation, policy.precision);
    }
    return result;
}

// Symbols come from the central unit registry, not an independent GUI map.
// An empty symbol intentionally represents dimensionless values.
inline QString suffix(const QString &symbol) {
    return symbol.isEmpty() ? QString() : QString(QChar(0x00a0)) + symbol;
}
inline QString heading(const QString &label, const QString &symbol) {
    return symbol.isEmpty() ? label : label + QStringLiteral(" (") + symbol + QChar(')');
}
inline std::optional<QString> measurement(std::optional<double> value, const QString &symbol,
                                         const QLocale &locale, const NumericFormatPolicy &policy = {}) {
    const std::optional<QString> number = format(value, locale, policy);
    if (!number) return std::nullopt;
    return *number + (value && std::isfinite(*value) ? suffix(symbol) : QString());
}
}
