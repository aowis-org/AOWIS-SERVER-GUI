#include "tab_map_editor_container.h"

MapEditorContainer::MapEditorContainer(MapModel *map_model, QWidget *parent)
    : QWidget{parent},
    layout( new QHBoxLayout(this) ),
    map_model( map_model ),
    map( new MapWidget(this->map_model, this) ),
    map_menu( new MapEditorMenuWidget(this->map, this) )
{
    setContentsMargins(0, 0, 0, 0);
    this->layout->setContentsMargins(0, 0, 0, 0);
    this->layout->setSpacing(0);
    
    QScrollArea *scroll_controls = new QScrollArea(this);
    scroll_controls->setMinimumWidth(160);
    scroll_controls->setMaximumWidth(180);
    scroll_controls->setWidgetResizable(true);
    scroll_controls->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_controls->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_controls->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    scroll_controls->setWidget(this->map_menu);
    
    this->layout->addWidget(scroll_controls);
    this->layout->addWidget(this->map);
}
MapWidget *MapEditorContainer::getMap()
{
    return this->map;
}





MapEditorMenuWidget::MapEditorMenuWidget(MapWidget *map, QWidget *parent)
    : QWidget{parent},
    layout( new QVBoxLayout(this) ),
    map( map ),
    map_nav( new MapNavigationWidget(this->map, this) ),
    toolbox( new QToolBox(this) )
{
    setMinimumWidth(160);
    setMaximumWidth(180);
    
    this->toolbox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    createToolboxCache(this->toolbox);
    createToolboxEdit(this->toolbox);
    
    this->layout->addWidget(this->map_nav);
    this->layout->addWidget(this->toolbox);
    this->layout->addStretch();
    
    this->toolbox->setCurrentIndex(1);
}

void MapEditorMenuWidget::createToolboxCache(QToolBox *tbx)
{
    QWidget *wgt = new QWidget(tbx);
    QVBoxLayout *lay = new QVBoxLayout(wgt);
    
    QLabel *label_explanation_rectangle = new QLabel("<b>Select Area</b> on<br>the map first:", this);
    
    QToolButton *btn_select_rectangle = new QToolButton(wgt);
    btn_select_rectangle->setText("Select Area");
    
    QLabel *label_explanation_actions = new QLabel("Than for the area you<br>have selected, you can<br>choose one of the<br>following actions:", this);
    
    QToolButton *btn_tiles_delete = new QToolButton(wgt);
    btn_tiles_delete->setText("Delete Tiles");
    btn_tiles_delete->setCheckable(true);
    btn_tiles_delete->setEnabled(false);
    
    QToolButton *btn_tiles_update = new QToolButton(wgt);
    btn_tiles_update->setText("Update");
    btn_tiles_update->setCheckable(true);
    btn_tiles_update->setEnabled(false);
    
    lay->addWidget(label_explanation_rectangle);
    lay->addWidget(btn_select_rectangle);
    lay->addWidget(label_explanation_actions);
    lay->addWidget(btn_tiles_delete);
    lay->addWidget(btn_tiles_update);
    
    tbx->addItem(wgt, "Tile Cache");
}
void MapEditorMenuWidget::createToolboxEdit(QToolBox *tbx)
{
    QWidget *wgt = new QWidget(tbx);
    QVBoxLayout *lay = new QVBoxLayout(wgt);
    
    QStringList labels = {
        "Select", "Delete Selected", "Add Note", "Add Reservoir", "Add Tank", "Add Pump", "Add Valve", "Add Junction", "Add Pipe", "Add Customer Point"
    };
    for (int i=0; i < labels.length(); i++)
    {
        if (i == 1)
        {
            QToolButton *btn = new QToolButton(wgt);
            btn->setText(labels[i]);
            lay->addWidget(btn);
        }
        else
        {
            QRadioButton *btn = new QRadioButton(labels[i], wgt);
            lay->addWidget(btn);
        }
    }
    
    tbx->addItem(wgt, "Edit Network");
}

