#include "entity_inspector_dock.h"


EntityInspectorDock::EntityInspectorDock(QWidget *parent)
    : QDockWidget("Entity Inspector", parent),
    label_title(new QLabel())
{
    //setMinimumWidth(Sizes::SidebarRightWidthBase);
    //setMaximumWidth(Sizes::SidebarRightWidthBase);
    
    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    QWidget *container = new QWidget(scroll);
    this->layout = new QVBoxLayout(container);
    this->layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    
    scroll->setWidget(container);
    setWidget(scroll);
    
    // Optional: restrict docking areas
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    
    // Optional: control features
    setFeatures(QDockWidget::DockWidgetClosable |
                QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable);
    
    this->layout->addWidget(this->label_title);
    
    showEntityTank();
    
    
    this->layout->addStretch();
}

void EntityInspectorDock::showEntityTank()
{
    this->label_title->setText("Tank");
    
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Water Tank Properties");
    
    QGridLayout *grid = new QGridLayout(group);
    
    QPixmap pixmap(":/icon/tower_large.png");
    
    QLabel *picture = new QLabel();
    picture->setPixmap(pixmap.scaledToHeight(
        Sizes::SidebarRightImageHeight,
        Qt::SmoothTransformation
    ));
    picture->setAlignment(Qt::AlignCenter);
    
    QLabel *label_name = new QLabel("Name");
    this->line_name = new QLineEdit();
    
    QPushButton *button_find = new QPushButton("Find on Map");
    
    QLabel *label_latitude = new QLabel("Latitude");
    this->spin_latitude = new QDoubleSpinBox;
    spin_latitude->setRange(-90.0, 90.0);
    spin_latitude->setDecimals(6);
    spin_latitude->setSingleStep(0.000001);
    spin_latitude->setSuffix(" °");
    spin_latitude->setAccelerated(true);
    
    QLabel *label_longitude = new QLabel("Longitude");
    this->spin_longitude = new QDoubleSpinBox;
    spin_longitude->setRange(-180.0, 180.0);
    spin_longitude->setDecimals(6);
    spin_longitude->setSingleStep(0.000001);
    spin_longitude->setSuffix(" °");
    spin_longitude->setAccelerated(true);
    
    QLabel *label_ground_elevation = new QLabel("Ground elevation");
    this->spin_ground_elevation = new QDoubleSpinBox;
    spin_ground_elevation->setRange(-10000.0, 10000.0);
    spin_ground_elevation->setDecimals(3);
    spin_ground_elevation->setSingleStep(0.10);
    spin_ground_elevation->setSuffix(" m");
    
    QLabel *label_tank_bottom_offset = new QLabel("Tank bottom<br>above ground");
    this->spin_tank_bottom_offset = new QDoubleSpinBox;
    spin_tank_bottom_offset->setRange(-100.0, 200.0);
    spin_tank_bottom_offset->setDecimals(3);
    spin_tank_bottom_offset->setSingleStep(0.10);
    spin_tank_bottom_offset->setSuffix(" m");
    
    QLabel *label_tank_bottom_elevation = new QLabel("Tank bottom elevation");
    this->spin_tank_bottom_elevation = new QDoubleSpinBox;
    spin_tank_bottom_elevation->setRange(-10000.0, 10000.0);
    spin_tank_bottom_elevation->setDecimals(3);
    spin_tank_bottom_elevation->setSingleStep(0.10);
    spin_tank_bottom_elevation->setSuffix(" m");
    //spin_tank_bottom_elevation->setReadOnly(true);
    connect(spin_tank_bottom_elevation, &QDoubleSpinBox::valueChanged, this, [this]
    {
        this->spin_ground_elevation->setEnabled(false);
        this->spin_tank_bottom_offset->setEnabled(false);
    });
    //spin_tank_bottom_elevation->setButtonSymbols(QAbstractSpinBox::NoButtons);
    
    QGroupBox *group_elevation = new QGroupBox("Elevation", this);
    QGridLayout *group_elevation_layout = new QGridLayout(group_elevation);
    group_elevation_layout->addWidget(label_ground_elevation, 20, 0);
    group_elevation_layout->addWidget(this->spin_ground_elevation, 20, 1);
    group_elevation_layout->addWidget(label_tank_bottom_offset, 21, 0);
    group_elevation_layout->addWidget(this->spin_tank_bottom_offset, 21, 1);
    group_elevation_layout->addWidget(label_tank_bottom_elevation, 22, 0);
    group_elevation_layout->addWidget(this->spin_tank_bottom_elevation, 22, 1);
    
    
    QLabel *label_overflow = new QLabel("Overflow Allowed");
    this->check_overflow = new QCheckBox();
    
    grid->addWidget(picture, 0, 0, 1, 2);
    
    grid->addWidget(label_name, 1, 0);
    grid->addWidget(this->line_name, 2, 0, 1, 2);
    
    grid->addWidget(label_latitude, 10, 0);
    grid->addWidget(this->spin_latitude, 10, 1);
    grid->addWidget(label_longitude, 11, 0);
    grid->addWidget(this->spin_longitude, 11, 1);
    grid->addWidget(button_find, 12, 0, 1, 2);
    
    
    grid->addWidget(group_elevation, 20, 0, 1, 2);
    
    grid->addWidget(label_overflow, 30, 0);
    grid->addWidget(this->check_overflow, 30, 1);
    
    this->layout->addWidget(group);
}

double EntityInspectorDock::calculateTankElevation()
{
    
}
