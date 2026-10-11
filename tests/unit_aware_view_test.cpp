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

    // A child of an inactive/hidden page must not refresh just because it
    // received a show() request while its parent was still hidden.
    QWidget stacked_page;
    QWidget nested_view(&stacked_page);
    int nested_refreshes = 0;
    UnitAwareView nested_binding(&nested_view, context, [&] { ++nested_refreshes; });
    nested_view.show();
    CHECK(nested_refreshes == 0);
    CHECK(context.update(QJsonObject{{QStringLiteral("pressure"), QStringLiteral("psi")}}));
    CHECK(nested_refreshes == 0);
    stacked_page.show();
    CHECK(nested_refreshes == 1);
    stacked_page.hide();

    // Hundreds of profile switches should coalesce into one update per
    // visible presentation region when the hidden page is shown again.
    const int previous_nested = nested_refreshes;
    for (int i = 0; i < 500; ++i) {
        CHECK(context.update(QJsonObject{{QStringLiteral("pressure"),
            (i % 2 == 0 ? QStringLiteral("bar") : QStringLiteral("psi"))}}));
    }
    CHECK(nested_refreshes == previous_nested);
    stacked_page.show();
    CHECK(nested_refreshes == previous_nested + 1);
    nested_binding.refreshIfNeeded();
    CHECK(nested_refreshes == previous_nested + 1);

    // A detached/destroyed view must not receive notification callbacks.
    int destroyed_refreshes = 0;
    {
        QWidget temporary_view;
        UnitAwareView temporary_binding(&temporary_view, context,
            [&] { ++destroyed_refreshes; });
        temporary_view.show();
        CHECK(destroyed_refreshes == 1);
    }
    CHECK(context.update(QJsonObject{{QStringLiteral("pressure"), QStringLiteral("kPa")}}));
    CHECK(destroyed_refreshes == 1);

    // Repeated transient dialog/page creation must not accumulate expired
    // listeners or retain their refresh closures between profile changes.
    const qsizetype live_views = context.subscriberCount();
    for (int i = 0; i < 500; ++i) {
        QWidget transient_page;
        UnitAwareView transient_binding(&transient_page, context, [] {});
        CHECK(context.subscriberCount() == live_views + 1);
    }
    CHECK(context.subscriberCount() == live_views);

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
