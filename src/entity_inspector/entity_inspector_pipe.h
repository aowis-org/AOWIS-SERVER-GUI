#ifndef ENTITY_INSPECTOR_PIPE_H
#define ENTITY_INSPECTOR_PIPE_H

#include <QObject>
#include <QWidget>
#include <QVBoxLayout>
#include <QGridLayout>

#include <QPixmap>
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
#include "../map/map_network_structs.h"

class EntityInspectorPipe : public EntityInspectorWidget
{
    Q_OBJECT
public:
    explicit EntityInspectorPipe(QWidget *parent = nullptr);
    
private:
    QVBoxLayout *layout = nullptr;
    QLabel *label = nullptr;
    
    QLabel *picture = nullptr;
    
    void addGroupDemands();
    
    
    void addGroupQuality();
    
    
    void addGroupSimMeas();
    
    
    void addGroupGraphs();
    
signals:
};

#endif // ENTITY_INSPECTOR_PIPE_H
