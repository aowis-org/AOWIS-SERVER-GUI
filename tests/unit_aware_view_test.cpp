#include "config/unit_aware_view.h"
#include "map/core/map_symbology_slider.h"

#include <QApplication>
#include <QJsonObject>
#include <QWidget>
#include "unit_test_check.h"

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    UnitPresentationContext context;
    context.initialize(QJsonObject{{QStringLiteral("pressure"), QStringLiteral("kPa")}});
    int visible_refreshes = 0;
    int hidden_refreshes = 0;
    QWidget visible;
    QWidget hidden;
    UnitAwareView visible_binding(&visible, context, [&] { ++visible_refreshes; });
    UnitAwareView hidden_binding(&hidden, context, [&] { ++hidden_refreshes; });
    CHECK(visible_refreshes == 0);
    CHECK(hidden_refreshes == 0);

    visible.show();
    CHECK(visible_refreshes == 1);
    CHECK(hidden_refreshes == 0);
    CHECK(context.update(QJsonObject{{QStringLiteral("pressure"), QStringLiteral("psi")}}));
    CHECK(visible_refreshes == 2);
    CHECK(hidden_refreshes == 0);
    CHECK(context.update(QJsonObject{{QStringLiteral("pressure"), QStringLiteral("bar")}}));
    CHECK(visible_refreshes == 3);
    CHECK(hidden_refreshes == 0);
    hidden.show();
    CHECK(hidden_refreshes == 1); // Coalesced directly to the latest generation.
    hidden_binding.refreshIfNeeded();
    CHECK(hidden_refreshes == 1);
    CHECK(!context.update(QJsonObject{{QStringLiteral("pressure"), QStringLiteral("bar")}}));
    CHECK(visible_refreshes == 3);

    visible.hide();
    CHECK(context.update(QJsonObject{{QStringLiteral("pressure"), QStringLiteral("kPa")}}));
    CHECK(visible_refreshes == 3);
    CHECK(hidden_refreshes == 2);
    visible.show();
    CHECK(visible_refreshes == 4);

    visible_binding.forceRefresh();
    CHECK(visible_refreshes == 5);

    // Unit changes are presentation-only; a world-space icon-size slider
    // must retain its integer-metre position and emit no valueChanged signal.
    MapSymbologySlider icon_size(25, 450, 150, QStringLiteral("Icon size"),
                                QStringLiteral(" m"));
    int value_changes = 0;
    QObject::connect(&icon_size, &QSlider::valueChanged, &icon_size,
                     [&value_changes](int) { ++value_changes; });
    icon_size.setDisplayPresentation(QStringLiteral(" ft"),
                                     3.280839895013123, 1);
    CHECK(icon_size.value() == 150);
    CHECK(icon_size.toolTip().contains(QStringLiteral("492.1 ft")));
    CHECK(value_changes == 0);
    icon_size.setDisplayPresentation(QStringLiteral(" m"), 1.0, 0);
    CHECK(icon_size.value() == 150 && value_changes == 0);
    return 0;
}
