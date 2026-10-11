#include "config/numeric_text.h"
#include "config/unit_presentation_context.h"
#include <QCoreApplication>
#include <limits>
#include <stdexcept>

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " #expression); } while (false)

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    const QLocale english(QLocale::English, QLocale::UnitedStates);
    const QLocale german(QLocale::German, QLocale::Germany);
    NumericFormatPolicy fixed{NumericFormatPolicy::Notation::Fixed, 2, false, true};
    CHECK(NumericText::format(1234.5, english, fixed) == QStringLiteral("1234.50"));
    CHECK(NumericText::format(1234.5, german, fixed) == QStringLiteral("1234,50"));
    fixed.grouping = true;
    CHECK(NumericText::format(1234.5, english, fixed) == QStringLiteral("1,234.50"));
    CHECK(NumericText::format(1234.5, german, fixed) == QStringLiteral("1.234,50"));
    CHECK(NumericText::parse(QStringLiteral("1.234,50"), german, true).value == 1234.5);
    CHECK(!NumericText::parse(QStringLiteral("1.234,50"), german).value);
    CHECK(!NumericText::parse(QStringLiteral("12,34.50"), english, true).value);
    CHECK(NumericText::parse(QStringLiteral("1,25 bar"), german, false, QStringLiteral("bar")).value == 1.25);
    CHECK(!NumericText::parse(QStringLiteral("1,25 psi"), german, false, QStringLiteral("bar")).value);
    CHECK(NumericText::parse(QStringLiteral("50 %"), english, false, QStringLiteral("%")).value == 50.0);
    CHECK(NumericText::parse(QStringLiteral("  "), english).status == NumericText::ParseStatus::Missing);
    CHECK(!NumericText::parse(QStringLiteral("1e"), english).value);
    CHECK(!NumericText::parse(QStringLiteral("1e9999"), english).value);
    CHECK(!NumericText::parse(QStringLiteral("NaN"), english).value);
    CHECK(!NumericText::parse(QStringLiteral("inf"), english).value);
    CHECK(NumericText::format(-0.0001, english, fixed) == QStringLiteral("0.00"));
    CHECK(NumericText::format(-0.0, english, fixed) == QStringLiteral("0.00"));
    CHECK(NumericText::format(std::nullopt, english) == QString(QChar(0x2014)));
    CHECK(NumericText::format(std::numeric_limits<double>::quiet_NaN(), english) == QStringLiteral("NaN"));
    CHECK(NumericText::format(std::numeric_limits<double>::infinity(), english) == QString(QChar(0x221e)));
    CHECK(NumericText::measurement(1.25, QStringLiteral("m"), english, fixed) == QStringLiteral("1.25") + QChar(0x00a0) + QStringLiteral("m"));
    CHECK(NumericText::suffix({}).isEmpty());
    CHECK(NumericText::heading(QStringLiteral("Flow"), QStringLiteral("L/s")) == QStringLiteral("Flow (L/s)"));
    NumericFormatPolicy exact;
    exact.precision = 17;
    for (const double value : {1.2345678901234567, 1e-200, 1e200}) {
        const std::optional<QString> text = NumericText::format(value, german, exact);
        CHECK(text && NumericText::parse(*text, german).value == value);
    }
    NumericFormatPolicy scientific{NumericFormatPolicy::Notation::Scientific, 3, false, true};
    CHECK(NumericText::format(1250.0, english, scientific) == QStringLiteral("1.250e+03"));
    exact.precision = 0;
    CHECK(!NumericText::format(1.0, english, exact));

    UnitPresentationContext context;
    QObject owner;
    int refreshes = 0;
    context.subscribe(&owner, [&](UnitPresentationContext::Generation) { ++refreshes; });
    const auto initial_generation = context.generation();
    CHECK(context.setFormatPolicy(fixed, QStringLiteral("pressure")));
    CHECK(context.generation() == initial_generation + 1 && refreshes == 1);
    CHECK(!context.setFormatPolicy(fixed, QStringLiteral("pressure")));
    CHECK(!context.setFormatPolicy(exact));
    CHECK(context.formatPolicy(QStringLiteral("pressure")) == fixed);
    CHECK(context.clearFormatPolicy(QStringLiteral("pressure")));
    CHECK(refreshes == 2);
    context.setLocale(german);
    const int locale_refreshes = refreshes;
    CHECK(!context.setLocale(german) && refreshes == locale_refreshes);
    return 0;
}
