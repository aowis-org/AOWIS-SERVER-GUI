#include "sim_control_dock.h"

#include "widgets/combo_checkboxes.h"

#include <QHBoxLayout>
#include <QWidget>

SimControlDock::SimControlDock(QWidget *parent)
    : QDockWidget(parent),
    content(new QWidget(this)),
    layout(new QHBoxLayout(content))
{
    this->setWidget(this->content);
    
    this->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    this->setFeatures(QDockWidget::NoDockWidgetFeatures);
    this->setTitleBarWidget(new QWidget(this));
    /*
    this->setFeatures(QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);
    */
    this->layout->setContentsMargins(8, 4, 8, 4);
    this->layout->setSpacing(8);
    
    ComboCheckboxes *combo = new ComboCheckboxes(this->content);
    combo->setMinimumWidth(250);
    
    combo->addItem(QStringLiteral("CHEMICAL"),
                   1,
                   true,
                   QStringLiteral("One dissolved constituent concentration, e.g. chlorine"));
    
    combo->addItem(QStringLiteral("AGE"),
                   1,
                   true,
                   QStringLiteral("Water age"));
    
    combo->addItem(QStringLiteral("TRACE"),
                   2,
                   true,
                   QStringLiteral("Source tracing: percent of water originating from one node"));
    
    this->layout->addWidget(combo, 0, Qt::AlignCenter);
}
