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
    
    GroupBoxCollapsible *group = new GroupBoxCollapsible("Attributes Water Tank");
    
    QGridLayout *grid = new QGridLayout(group);
    
    QPixmap pixmap(":/icon/tower_large.png");
    
    QLabel *picture = new QLabel();
    picture->setPixmap(pixmap.scaledToHeight(
        Sizes::SidebarRightImageHeight,
        Qt::SmoothTransformation
    ));
    picture->setAlignment(Qt::AlignCenter);
    
    QLabel *label_name = new QLabel("Name");
    QLineEdit *line_name = new QLineEdit();
    
    QPushButton *button_find = new QPushButton("Find on Map");
    
    QLabel *label_coords_lat = new QLabel("Latitude");
    QLabel *label_coords_lon = new QLabel("Longitude");
    QLabel *label_coords_lat_value = new QLabel();
    QLabel *label_coords_lon_value = new QLabel();
    
    QLabel *label_elevation = new QLabel("Elevation [m]");
    QDoubleSpinBox *spin_elevation = new QDoubleSpinBox;
    spin_elevation->setRange(-10000.0, 10000.0);
    spin_elevation->setDecimals(3);
    spin_elevation->setSingleStep(0.10);
    spin_elevation->setSuffix(" m");
    
    QLabel *label_elevation_ground = new QLabel("Elevation Ground [m]");
    QDoubleSpinBox *spin_elevation_ground = new QDoubleSpinBox;
    spin_elevation_ground->setRange(-10000.0, 10000.0);
    spin_elevation_ground->setDecimals(3);
    spin_elevation_ground->setSingleStep(0.10);
    spin_elevation_ground->setSuffix(" m");
    
    
    QLabel *label_overflow = new QLabel("Overflow Allowed");
    QCheckBox *check_overflow = new QCheckBox();
    
    grid->addWidget(picture, 0, 0, 1, 2);
    
    grid->addWidget(label_name, 1, 0);
    grid->addWidget(line_name, 2, 0, 1, 2);
    
    grid->addWidget(label_coords_lat, 10, 0);
    grid->addWidget(label_coords_lat_value, 10, 1);
    grid->addWidget(label_coords_lon, 11, 0);
    grid->addWidget(label_coords_lon_value, 11, 1);
    grid->addWidget(button_find, 12, 0, 1, 2);
    
    
    
    grid->addWidget(label_elevation, 20, 0);
    grid->addWidget(spin_elevation, 20, 1);
    grid->addWidget(label_elevation_ground, 21, 0);
    grid->addWidget(spin_elevation_ground, 21, 1);
    
    grid->addWidget(label_overflow, 30, 0);
    grid->addWidget(check_overflow, 30, 1);
    
    this->layout->addWidget(group);
}


