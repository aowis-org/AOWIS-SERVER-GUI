#include "tab_tanks_widget.h"

TanksWidget::TanksWidget(QWidget *parent)
    : QWidget{parent},
    layout( new QVBoxLayout(this) ),
    table( new QTableWidget(this) )
{
    setLayout(this->layout);
    
    this->table->setColumnCount(8);
    this->table->setHorizontalHeaderLabels({"ID", "Elevation", "InitLvl", "MinLvl", "MaxLvl", "Diameter", "Curve", "Overflow"});
    this->table->setAlternatingRowColors(true);
    this->table->resizeColumnsToContents();
    
    this->layout->addWidget(this->table);
    
}
