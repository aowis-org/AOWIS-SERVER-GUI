#ifndef ENTITY_INSPECTOR_JUNCTION_H
#define ENTITY_INSPECTOR_JUNCTION_H

#include <QObject>
#include <QWidget>

#include <QVBoxLayout>
#include <QGridLayout>

#include <QPixmap>
#include <QIcon>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>

#include "entity_inspector_widget.h"

#include "../widgets/group_box_collapsible.h"

#include "../_enums_structs.h"
#include "../_sizes.h"
#include "../map/map_models.h"

class EntityInspectorJunction : public EntityInspectorWidget
{
    Q_OBJECT
public:
    explicit EntityInspectorJunction(QWidget *parent = nullptr);
    
private:
    QLabel *picture = nullptr;
    
    void addGroupQuality();
    
    
signals:
};

#endif // ENTITY_INSPECTOR_JUNCTION_H
