#include "widgets/group_box_collapsible.h"

#include <QAbstractButton>
#include <QChildEvent>
#include <QLayout>
#include <QResizeEvent>
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

void GroupBoxCollapsible::setHeaderAction(QAbstractButton *button)
{
    this->header_action = button;
    if (button == nullptr) return;
    button->setParent(this);
    button->setFixedSize(22, 22);
    button->move(width() - button->width() - 8, 1);
    button->raise();
    button->show();
}

void GroupBoxCollapsible::resizeEvent(QResizeEvent *event)
{
    QGroupBox::resizeEvent(event);
    if (this->header_action != nullptr)
        this->header_action->move(width() - this->header_action->width() - 8, 1);
}

void GroupBoxCollapsible::applyExpandedState(bool expanded)
{
    const auto children = findChildren<QWidget *>(
        QString(),
        Qt::FindDirectChildrenOnly
        );
    
    for (QWidget *child : children)
        if (child != this->header_action)
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
    
    if (QLayout *child_layout = qobject_cast<QLayout *>(event->child()))
    {
        // Every group's own QGridLayout/QVBoxLayout otherwise falls back to the
        // style's default layout margin/spacing (commonly ~9-11px per side). With
        // several group boxes stacked inside a fixed-width sidebar panel, those
        // untouched defaults add up to real, avoidable width.
        child_layout->setContentsMargins(6, 4, 6, 6);
        child_layout->setSpacing(4);
        return;
    }
    
    if (!isCollapsed())
        return;
    
    auto *widget = qobject_cast<QWidget *>(event->child());
    
    if (widget && widget != this->header_action)
        widget->setVisible(false);
}
