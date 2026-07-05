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
    
    setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    setFeatures(QDockWidget::NoDockWidgetFeatures);
    setTitleBarWidget(new QWidget(this));
    
    content->setFixedHeight(Sizes::SimControlDockHeight);
    setFixedHeight(Sizes::SimControlDockHeight);
    
    /*
    this->setFeatures(QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);
    */
    //this->layout->setContentsMargins(8, 4, 8, 4);
    //this->layout->setSpacing(8);
    
    this->layout->addStretch();
    
    addChemicalQualityDropdown();
    addHeadlossFormulaDropdown();
    
    this->layout->addStretch();
}

void SimControlDock::addChemicalQualityDropdown()
{
    ComboCheckboxes *combo = new ComboCheckboxes(this->content);
    combo->setMinimumWidth(210);
    combo->setMaximumWidth(210);
    
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
    
    QLabel *label_quality_dropdown = new QLabel("Water Quality Modes: ");
    this->layout->addWidget(label_quality_dropdown);
    
    //this->layout->addWidget(combo, 0, Qt::AlignCenter);
    this->layout->addWidget(combo);
}

void SimControlDock::addHeadlossFormulaDropdown()
{
    ComboCheckboxes *combo = new ComboCheckboxes(this->content);
    combo->setMinimumWidth(210);
    combo->setMaximumWidth(210);
    
    combo->addItem(QStringLiteral("Hazen-Williams"),
                   1,
                   true,
                   QStringLiteral("Run simulation with the Hazen-Williams headloss formula.<br><br>Requires pipe roughness coefficient C."));
    
    combo->addItem(QStringLiteral("Darcy-Weisbach"),
                   2,
                   false,
                   QStringLiteral("Run simulation with the Darcy-Weisbach headloss formula.<br><br>Requires absolute pipe roughness ε in mm."));
    
    combo->addItem(QStringLiteral("Chezy-Manning"),
                   3,
                   false,
                   QStringLiteral("Run simulation with the Chezy-Manning headloss formula.<br><br>Requires Manning roughness coefficient n."));
    
    this->layout->addWidget(combo);
}

