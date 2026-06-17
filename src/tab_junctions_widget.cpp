#include "tab_junctions_widget.h"

JunctionsWidget::JunctionsWidget(QWidget *parent)
    : QWidget{parent},
    layout( new QVBoxLayout(this) ),
    table( new QTableWidget(this) )
{
    setLayout(this->layout);
    
    this->table->setColumnCount(2);
    this->table->setHorizontalHeaderLabels({"ID", "Elevation"});
    this->table->setAlternatingRowColors(true);
    this->table->resizeColumnsToContents(); 
    
    this->layout->addWidget(this->table);
}
