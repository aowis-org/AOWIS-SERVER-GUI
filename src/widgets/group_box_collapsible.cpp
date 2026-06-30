#include "group_box_collapsible.h"

#include <QChildEvent>
#include <QLayout>
#include <QWidget>

GroupBoxCollapsible::GroupBoxCollapsible(QWidget *parent)
    : GroupBoxCollapsible(QString(), parent)
{
    
}

GroupBoxCollapsible::GroupBoxCollapsible(const QString &title, QWidget *parent)
    : QGroupBox(title, parent)
{
    setCheckable(true);
    setChecked(true);
    
    connect(this, &QGroupBox::toggled,
            this, &GroupBoxCollapsible::applyExpandedState);
}

void GroupBoxCollapsible::setCollapsed(bool collapsed)
{
    setChecked(!collapsed);
    applyExpandedState(!collapsed);
}

bool GroupBoxCollapsible::isCollapsed() const
{
    return !isChecked();
}

void GroupBoxCollapsible::applyExpandedState(bool expanded)
{
    const auto children = findChildren<QWidget *>(
        QString(),
        Qt::FindDirectChildrenOnly
        );
    
    for (QWidget *child : children)
        child->setVisible(expanded);
    
    if (layout())
        layout()->invalidate();
    
    updateGeometry();
    
    if (expanded)
        emit signalExpanded(this);
    else
        emit signalCollapsed(this);
}

void GroupBoxCollapsible::childEvent(QChildEvent *event)
{
    QGroupBox::childEvent(event);
    
    if (!event->added())
        return;
    
    if (!isCollapsed())
        return;
    
    auto *widget = qobject_cast<QWidget *>(event->child());
    
    if (widget)
        widget->setVisible(false);
}
