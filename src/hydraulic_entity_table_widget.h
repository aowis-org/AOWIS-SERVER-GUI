#ifndef HYDRAULIC_ENTITY_TABLE_WIDGET_H
#define HYDRAULIC_ENTITY_TABLE_WIDGET_H

#include <QString>
#include <QWidget>

#include "_enums_structs.h"

class HydraulicData;
class HydraulicEntityTableModel;
class QLabel;
class QModelIndex;
class QSortFilterProxyModel;
class QTableView;

class HydraulicEntityTableWidget : public QWidget
{
    Q_OBJECT

public:
    explicit HydraulicEntityTableWidget(HydraulicData *hydraulic_data,
                                        InfrastructureEntity entity_type,
                                        const QString &entity_plural,
                                        QWidget *parent = nullptr);

private:
    void openEntity(const QModelIndex &proxy_index);

    HydraulicData *hydraulic_data;
    InfrastructureEntity entity_type;
    QLabel *label_help;
    QTableView *table;
    HydraulicEntityTableModel *model;
    QSortFilterProxyModel *proxy_model;
};

#endif // HYDRAULIC_ENTITY_TABLE_WIDGET_H
