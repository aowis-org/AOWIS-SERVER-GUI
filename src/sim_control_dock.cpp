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
    
    addFlowUnitCombo();
    addChemicalQualityDropdown();
    addHeadlossFormulaDropdown();
    
    QPushButton *button_sim_start = new QPushButton(this);
    button_sim_start->setIcon(QIcon(":/icon/simulation_start.png"));
    button_sim_start->setFlat(true);
    button_sim_start->setIconSize(QSize(30, 30));
    button_sim_start->setMaximumSize(30, 30);
    button_sim_start->setContentsMargins(0, 0, 0, 0);
    button_sim_start->setToolTip("Run Configured Simulations");
    
    connect(button_sim_start, &QPushButton::clicked, this, [this]
    {
        emit signalSimulationStart();
    });
    
    this->layout->addWidget(button_sim_start);
    
    this->layout->addStretch();
}

void SimControlDock::addFlowUnitCombo()
{
    QLabel *label_flow_units = new QLabel("Flow Units:");
    this->layout->addWidget(label_flow_units);
    
    QToolButton *button_flow_units = new QToolButton(this);
    button_flow_units->setText("CMH");
    button_flow_units->setPopupMode(QToolButton::InstantPopup);
    
    QMenu *menu_flow_units = new QMenu(button_flow_units);
    
    QAction *action_cmh = menu_flow_units->addAction("CMH — cubic meters per hour");
    action_cmh->setData(static_cast<int>(EN_CMH));
    
    QAction *action_lps = menu_flow_units->addAction("LPS — liters per second");
    action_lps->setData(static_cast<int>(EN_LPS));
    
    menu_flow_units->addSeparator();
    
    QMenu *menu_other_metric = menu_flow_units->addMenu("Other metric");
    
    QAction *action_lpm = menu_other_metric->addAction("LPM — liters per minute");
    action_lpm->setData(static_cast<int>(EN_LPM));
    
    QAction *action_mld = menu_other_metric->addAction("MLD — million liters per day");
    action_mld->setData(static_cast<int>(EN_MLD));
    
    QAction *action_cmd = menu_other_metric->addAction("CMD — cubic meters per day");
    action_cmd->setData(static_cast<int>(EN_CMD));
    
    QAction *action_cms = menu_other_metric->addAction("CMS — cubic meters per second");
    action_cms->setData(static_cast<int>(EN_CMS));
    
    QMenu *menu_imperial = menu_flow_units->addMenu("Imperial / US");
    
    QAction *action_cfs = menu_imperial->addAction("CFS — cubic feet per second");
    action_cfs->setData(static_cast<int>(EN_CFS));
    
    QAction *action_gpm = menu_imperial->addAction("GPM — gallons per minute");
    action_gpm->setData(static_cast<int>(EN_GPM));
    
    QAction *action_mgd = menu_imperial->addAction("MGD — million gallons per day");
    action_mgd->setData(static_cast<int>(EN_MGD));
    
    QAction *action_imgd = menu_imperial->addAction("IMGD — imperial million gallons per day");
    action_imgd->setData(static_cast<int>(EN_IMGD));
    
    QAction *action_afd = menu_imperial->addAction("AFD — acre-feet per day");
    action_afd->setData(static_cast<int>(EN_AFD));
    
    button_flow_units->setMenu(menu_flow_units);
    
    
    const QList<QMenu *> menus = {
        menu_flow_units,
        menu_other_metric,
        menu_imperial
    };
    
    for (QMenu *menu : menus)
    {
        connect(menu, &QMenu::triggered, this,
            [this, button_flow_units](QAction *action)
            {
                if (action == nullptr)
                    return;
                
                if (action->menu() != nullptr)
                    return;
                
                bool ok = false;
                int value = action->data().toInt(&ok);
                
                if (!ok)
                    return;
                
                EN_FlowUnits flow_units = static_cast<EN_FlowUnits>(value);
                
                QString label = action->text().section(" ", 0, 0);
                button_flow_units->setText(label);
                
                this->selected_flow_units = flow_units;
            });
    }
    
    this->layout->addWidget(button_flow_units);
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

