#include "map/core/browser_map_scale_units.h"
#include <QCoreApplication>
#include "unit_test_check.h"

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    UnitPresentationContext context;
    context.setLocale(QLocale::c());
    const auto distance = [&context](const QString &label) {
        for (const QJsonValue &entry : BrowserMapScaleUnits::choices(context))
        {
            const QJsonObject choice = entry.toObject();
            if (choice.value(QStringLiteral("label")).toString() == label)
                return choice.value(QStringLiteral("meters")).toDouble();
        }
        return -1.0;
    };
    context.initialize({{QStringLiteral("length"), QStringLiteral("m")}});
    CHECK(distance(QStringLiteral("100 m")) == 100.0);
    CHECK(distance(QStringLiteral("1 km")) == 1000.0);
    CHECK(distance(QStringLiteral("1000 m")) < 0.0);
    context.update({{QStringLiteral("length"), QStringLiteral("ft")}});
    CHECK(std::abs(distance(QStringLiteral("100 ft")) - 30.48) < 1e-10);
    CHECK(std::abs(distance(QStringLiteral("1 mi")) - 1609.344) < 1e-10);
    context.update({{QStringLiteral("length"), QStringLiteral("cm")}});
    // A known unit is not necessarily allowed for this quantity.
    // The Model length registry permits m, ft, km and mi, but not cm.
    CHECK(BrowserMapScaleUnits::choices(context).isEmpty());
    context.update({{QStringLiteral("length"), QStringLiteral("km")}});
    CHECK(std::abs(distance(QStringLiteral("0.5 km")) - 500.0) < 1e-10);
    context.update({{QStringLiteral("length"), QStringLiteral("mi")}});
    CHECK(std::abs(distance(QStringLiteral("1 mi")) - 1609.344) < 1e-10);
    context.update({{QStringLiteral("length"), QStringLiteral("km")}});
    context.setLocale(QLocale(QLocale::German));
    CHECK(std::abs(distance(QStringLiteral("0,5 km")) - 500.0) < 1e-10);
    context.update({{QStringLiteral("length"), QStringLiteral("invalid")}});
    CHECK(BrowserMapScaleUnits::choices(context).isEmpty());
    return 0;
}
