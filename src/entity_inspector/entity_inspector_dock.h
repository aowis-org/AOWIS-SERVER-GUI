#ifndef ENTITY_INSPECTOR_DOCK_H
#define ENTITY_INSPECTOR_DOCK_H

#include <QWidget>
#include <QDockWidget>

#include <QScrollArea>
#include <QPixmap>
#include <QPushButton>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>

#include <QVBoxLayout>

#include "../widgets/group_box_collapsible.h"

#include "../_enums_structs.h"
#include "../_sizes.h"
#include "../map_network_structs.h"

class EntityInspectorDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit EntityInspectorDock(QWidget *parent = nullptr);
    
    //void showEntity(MapNetworkStructs &entity);
    
    void showEntityTank();
    
private:
    QVBoxLayout *layout = nullptr;
    
    QLabel *label_title = nullptr;
};

#endif // ENTITY_INSPECTOR_DOCK_H
