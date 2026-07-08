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
    
    QPushButton *button_sim_start = new QPushButton(this);
    button_sim_start->setIcon(QIcon(":/icon/simulation_start.png"));
    button_sim_start->setFlat(true);
    button_sim_start->setIconSize(QSize(30, 30));
    button_sim_start->setMaximumSize(30, 30);
    button_sim_start->setContentsMargins(0, 0, 0, 0);
    button_sim_start->setToolTip("Run Configured Simulations");
    
    this->layout->addWidget(button_sim_start);
    
    this->layout->addStretch();
}

void SimControlDock::addChemicalQualityDropdown()
{
    QLabel *label_quality_dropdown = new QLabel("Water Quality Modes:");
    this->layout->addWidget(label_quality_dropdown);
    
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
    
    //this->layout->addWidget(combo, 0, Qt::AlignCenter);
    this->layout->addWidget(combo);
}

void SimControlDock::addHeadlossFormulaDropdown()
{
    QLabel *label_quality_dropdown = new QLabel("Headloss Formulas:");
    this->layout->addWidget(label_quality_dropdown);
    
    this->combo_headloss_formula = new ComboCheckboxes(this->content);
    this->combo_headloss_formula->setMinimumWidth(210);
    this->combo_headloss_formula->setMaximumWidth(210);
    
    this->combo_headloss_formula->addItem(
        QStringLiteral("Hazen-Williams"),
        static_cast<int>(HeadlossFormula::HazenWilliams),
        true,
        QStringLiteral("Run simulation with the Hazen-Williams headloss formula.<br><br>Requires pipe roughness coefficient C.")
        );
    
    this->combo_headloss_formula->addItem(
        QStringLiteral("Darcy-Weisbach"),
        static_cast<int>(HeadlossFormula::DarcyWeisbach),
        false,
        QStringLiteral("Run simulation with the Darcy-Weisbach headloss formula.<br><br>Requires absolute pipe roughness ε in mm.")
        );
    
    this->combo_headloss_formula->addItem(
        QStringLiteral("Chezy-Manning"),
        static_cast<int>(HeadlossFormula::ChezyManning),
        false,
        QStringLiteral("Run simulation with the Chezy-Manning headloss formula.<br><br>Requires Manning roughness coefficient n.")
        );
    
    connect(this->combo_headloss_formula, &ComboCheckboxes::checkedItemsChanged, this, [this]
    {
        const QList<int> indexes_checked = this->combo_headloss_formula->checkedIndexes();
        
        HeadlossFormulas formulas = HeadlossFormula::None;
        
        for (const int index : indexes_checked)
        {
            int value = this->combo_headloss_formula->itemData(index).toInt();
            HeadlossFormula formula = static_cast<HeadlossFormula>(value);
            
            formulas |= formula;
        }
        
        emit signalHeadlossFormulaChanged(formulas);
    });
    
    this->layout->addWidget(this->combo_headloss_formula);
}

