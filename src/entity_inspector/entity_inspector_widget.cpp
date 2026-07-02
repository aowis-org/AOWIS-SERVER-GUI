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

void EntityInspectorWidget::addGroupPosition()
{
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Position");
    QGridLayout *grid = new QGridLayout(group);
    
    QLabel *label_latitude = new QLabel("Latitude");
    this->spin_latitude = new QDoubleSpinBox;
    this->spin_latitude->setRange(-90.0, 90.0);
    this->spin_latitude->setDecimals(6);
    this->spin_latitude->setSingleStep(0.000001);
    this->spin_latitude->setSuffix(" °");
    this->spin_latitude->setAccelerated(true);
    
    QLabel *label_longitude = new QLabel("Longitude");
    this->spin_longitude = new QDoubleSpinBox;
    this->spin_longitude->setRange(-180.0, 180.0);
    this->spin_longitude->setDecimals(6);
    this->spin_longitude->setSingleStep(0.000001);
    this->spin_longitude->setSuffix(" °");
    this->spin_longitude->setAccelerated(true);
    
    QPushButton *button_find = new QPushButton("Find on Map");
    
    grid->addWidget(label_latitude, 0, 0);
    grid->addWidget(this->spin_latitude, 0, 1);
    grid->addWidget(label_longitude, 1, 0);
    grid->addWidget(this->spin_longitude, 1, 1);
    grid->addWidget(button_find, 2, 0, 1, 2);
    
    this->layout_main->addWidget(group);
}
