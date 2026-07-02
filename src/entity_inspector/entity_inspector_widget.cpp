#include "entity_inspector_widget.h"

#include <QGridLayout>
#include <QPixmap>

EntityInspectorWidget::EntityInspectorWidget(QWidget *parent)
    : QWidget(parent),
    layout_main(new QVBoxLayout(this)),
    label_title(new QLabel(this))
{
    this->layout_main->addWidget(this->label_title);
}

QVBoxLayout *EntityInspectorWidget::mainLayout() const
{
    return this->layout_main;
}

void EntityInspectorWidget::setTitle(const QString &title)
{
    this->label_title->setText("<b>" + title.toHtmlEscaped() + "</b>");
}

void EntityInspectorWidget::addGroupGeneral(const QString &icon_path, const QString &name)
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("General");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *picture = new QLabel();
    QPixmap pixmap(icon_path);
    
    picture->setPixmap(pixmap.scaledToHeight(
        Sizes::SidebarRightImageHeight,
        Qt::SmoothTransformation
        ));
    picture->setAlignment(Qt::AlignCenter);
    
    QLabel *label_name = new QLabel("Name");
    this->line_name = new QLineEdit();
    this->line_name->setText(name);
    
    grid->addWidget(picture, 0, 0, 1, 2);
    grid->addWidget(label_name, 1, 0);
    grid->addWidget(this->line_name, 2, 0, 1, 2);
    
    this->layout_main->addWidget(group);
}
