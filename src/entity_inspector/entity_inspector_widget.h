#ifndef ENTITY_INSPECTOR_WIDGET_H
#define ENTITY_INSPECTOR_WIDGET_H

#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QString>
#include <QDoubleSpinBox>
#include <QPushButton>

#include "../widgets/group_box_collapsible.h"
#include "../_sizes.h"

class EntityInspectorWidget : public QWidget
{
    Q_OBJECT
    
public:
    explicit EntityInspectorWidget(QWidget *parent = nullptr);
    
protected:
    QVBoxLayout *mainLayout() const;
    
    void setTitle(const QString &title);
    void addGroupGeneral(const QString &icon_path, const QString &name);
    QLineEdit *line_name = nullptr;
    
    void addGroupPosition();
    QDoubleSpinBox *spin_latitude = nullptr;
    QDoubleSpinBox *spin_longitude = nullptr;
    
private:
    QVBoxLayout *layout_main = nullptr;
    QLabel *label_title = nullptr;
};

#endif
