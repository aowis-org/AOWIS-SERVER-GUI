#include "top_control_bar.h"

#include "_sizes.h"
#include "widgets/combo_checkboxes.h"

#include <QAction>
#include <QComboBox>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLayout>
#include <QLabel>
#include <QMenu>
#include <QPalette>
#include <QPushButton>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
class TopControlBarContent : public QWidget
{
public:
    explicit TopControlBarContent(QWidget *parent = nullptr)
        : QWidget(parent),
        left_group(new QWidget(this)),
        center_group(new QWidget(this)),
        right_group(new QWidget(this)),
        left_layout(new QHBoxLayout(this->left_group)),
        center_layout(new QHBoxLayout(this->center_group)),
        right_layout(new QHBoxLayout(this->right_group))
    {
        this->left_layout->setContentsMargins(0, 0, 0, 0);
        this->left_layout->setSpacing(10);

        this->center_layout->setContentsMargins(0, 0, 0, 0);
        this->center_layout->setSpacing(10);

        this->right_layout->setContentsMargins(0, 0, 0, 0);
        this->right_layout->setSpacing(10);

        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumHeight(Sizes::TopControlBarHeight);
    }

    QHBoxLayout *leftLayout() const
    {
        return this->left_layout;
    }

    QHBoxLayout *centerLayout() const
    {
        return this->center_layout;
    }

    QHBoxLayout *rightLayout() const
    {
        return this->right_layout;
    }

    QSize sizeHint() const override
    {
        const int width = this->left_group->sizeHint().width()
                          + this->center_group->sizeHint().width()
                          + this->right_group->sizeHint().width()
                          + 64;
        return QSize(width, Sizes::TopControlBarHeight);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);

        constexpr int side_margin = 10;
        constexpr int group_spacing = 14;

        const QSize left_size = this->left_group->sizeHint();
        const QSize center_size = this->center_group->sizeHint();
        const QSize right_size = this->right_group->sizeHint();

        const int left_y = qMax(0, (height() - left_size.height()) / 2);
        const int center_y = qMax(0, (height() - center_size.height()) / 2);
        const int right_y = qMax(0, (height() - right_size.height()) / 2);

        const int left_x = side_margin;
        const int right_x = width() - side_margin - right_size.width();

        const int centered_x = (width() - center_size.width()) / 2;
        const int minimum_center_x = left_x + left_size.width() + group_spacing;
        const int maximum_center_x = right_x - group_spacing - center_size.width();

        int center_x = centered_x;
        if (minimum_center_x <= maximum_center_x)
            center_x = qBound(minimum_center_x, centered_x, maximum_center_x);
        else
            center_x = minimum_center_x;

        this->left_group->setGeometry(left_x, left_y, left_size.width(), left_size.height());
        this->center_group->setGeometry(center_x, center_y, center_size.width(), center_size.height());
        this->right_group->setGeometry(right_x, right_y, right_size.width(), right_size.height());
    }

private:
    QWidget *left_group = nullptr;
    QWidget *center_group = nullptr;
    QWidget *right_group = nullptr;

    QHBoxLayout *left_layout = nullptr;
    QHBoxLayout *center_layout = nullptr;
    QHBoxLayout *right_layout = nullptr;
};

QLabel *createCaption(const QString &text, QWidget *parent)
{
    QLabel *label = new QLabel(text, parent);
    QFont font = label->font();

    if (font.pointSizeF() > 0.0)
        font.setPointSizeF(qMax(8.0, font.pointSizeF() - 2.0));
    else
        font.setPixelSize(10);

    label->setFont(font);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, palette.color(QPalette::PlaceholderText));
    label->setPalette(palette);

    return label;
}

QWidget *createLabeledControl(const QString &caption, QWidget *control, QWidget *parent)
{
    QWidget *container = new QWidget(parent);
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);
    layout->addWidget(createCaption(caption, container));
    layout->addWidget(control);
    return container;
}

QFrame *createSeparator(QWidget *parent)
{
    QFrame *separator = new QFrame(parent);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Sunken);
    separator->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    return separator;
}

TopControlBarContent *barContent(QWidget *content)
{
    return static_cast<TopControlBarContent *>(content);
}
}

TopControlBar::TopControlBar(QWidget *parent)
    : QToolBar(parent),
    content(new TopControlBarContent(this))
{
    setObjectName(QStringLiteral("top_control_bar"));
    setAllowedAreas(Qt::TopToolBarArea);
    setMovable(false);
    setFloatable(false);
    setContextMenuPolicy(Qt::PreventContextMenu);
    setIconSize(QSize(30, 30));
    setFixedHeight(Sizes::TopControlBarHeight);
    setContentsMargins(0, 0, 0, 0);

    if (layout() != nullptr)
        layout()->setContentsMargins(0, 0, 0, 0);

    this->content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    addWidget(this->content);

    addProjectControls();
    addFlowUnitCombo();
    addChemicalQualityDropdown();
    addHeadlossFormulaDropdown();
    addSimulationControls();
    addViewControls();
}

void TopControlBar::setFullScreenState(bool fullscreen)
{
    if (this->button_fullscreen == nullptr)
        return;

    const QStyle::StandardPixmap icon = fullscreen
                                           ? QStyle::SP_TitleBarNormalButton
                                           : QStyle::SP_TitleBarMaxButton;

    this->button_fullscreen->setIcon(style()->standardIcon(icon));
    this->button_fullscreen->setToolTip(fullscreen
                                            ? QStringLiteral("Leave fullscreen [F11]")
                                            : QStringLiteral("Enter fullscreen [F11]"));
}

void TopControlBar::addProjectControls()
{
    TopControlBarContent *bar_content = barContent(this->content);

    QComboBox *combo_project = new QComboBox(this->content);
    combo_project->setFixedWidth(210);
    combo_project->addItem(QStringLiteral("Select project"));

    QComboBox *combo_revision = new QComboBox(this->content);
    combo_revision->setFixedWidth(160);
    combo_revision->addItem(QStringLiteral("Select revision"));

    bar_content->leftLayout()->addWidget(createLabeledControl(QStringLiteral("Project"), combo_project, this->content));
    bar_content->leftLayout()->addWidget(createLabeledControl(QStringLiteral("Revision"), combo_revision, this->content));
}

void TopControlBar::addFlowUnitCombo()
{
    TopControlBarContent *bar_content = barContent(this->content);
    bar_content->centerLayout()->addWidget(createSeparator(this->content));

    QToolButton *button_flow_units = new QToolButton(this->content);
    button_flow_units->setText(QStringLiteral("CMH"));
    button_flow_units->setFixedSize(82, 30);
    button_flow_units->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button_flow_units->setPopupMode(QToolButton::InstantPopup);

    QMenu *menu_flow_units = new QMenu(button_flow_units);
    button_flow_units->setMenu(menu_flow_units);

    QAction *action_cmh = menu_flow_units->addAction(QStringLiteral("CMH — cubic meters per hour"));
    action_cmh->setData(static_cast<int>(EN_CMH));

    QAction *action_lps = menu_flow_units->addAction(QStringLiteral("LPS — liters per second"));
    action_lps->setData(static_cast<int>(EN_LPS));

    menu_flow_units->addSeparator();

    QMenu *menu_other_metric = menu_flow_units->addMenu(QStringLiteral("Other metric"));

    QAction *action_lpm = menu_other_metric->addAction(QStringLiteral("LPM — liters per minute"));
    action_lpm->setData(static_cast<int>(EN_LPM));

    QAction *action_mld = menu_other_metric->addAction(QStringLiteral("MLD — million liters per day"));
    action_mld->setData(static_cast<int>(EN_MLD));

    QAction *action_cmd = menu_other_metric->addAction(QStringLiteral("CMD — cubic meters per day"));
    action_cmd->setData(static_cast<int>(EN_CMD));

    QAction *action_cms = menu_other_metric->addAction(QStringLiteral("CMS — cubic meters per second"));
    action_cms->setData(static_cast<int>(EN_CMS));

    QMenu *menu_imperial = menu_flow_units->addMenu(QStringLiteral("Imperial / US"));

    QAction *action_cfs = menu_imperial->addAction(QStringLiteral("CFS — cubic feet per second"));
    action_cfs->setData(static_cast<int>(EN_CFS));

    QAction *action_gpm = menu_imperial->addAction(QStringLiteral("GPM — gallons per minute"));
    action_gpm->setData(static_cast<int>(EN_GPM));

    QAction *action_mgd = menu_imperial->addAction(QStringLiteral("MGD — million gallons per day"));
    action_mgd->setData(static_cast<int>(EN_MGD));

    QAction *action_imgd = menu_imperial->addAction(QStringLiteral("IMGD — imperial million gallons per day"));
    action_imgd->setData(static_cast<int>(EN_IMGD));

    QAction *action_afd = menu_imperial->addAction(QStringLiteral("AFD — acre-feet per day"));
    action_afd->setData(static_cast<int>(EN_AFD));

    connect(menu_flow_units, &QMenu::triggered, this, [this, button_flow_units](QAction *action)
    {
        if (action == nullptr || action->menu() != nullptr)
            return;

        bool ok = false;
        const int value = action->data().toInt(&ok);

        if (!ok)
            return;

        this->selected_flow_units = static_cast<EN_FlowUnits>(value);
        button_flow_units->setText(action->text().section(' ', 0, 0));
    });

    this->selected_flow_units = EN_CMH;
    bar_content->centerLayout()->addWidget(createLabeledControl(QStringLiteral("Flow units"), button_flow_units, this->content));
}

void TopControlBar::addChemicalQualityDropdown()
{
    TopControlBarContent *bar_content = barContent(this->content);

    ComboCheckboxes *combo = new ComboCheckboxes(this->content);
    combo->setFixedWidth(190);
    combo->setSummaryLimit(2);

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

    bar_content->centerLayout()->addWidget(createLabeledControl(QStringLiteral("Water quality"), combo, this->content));
}

void TopControlBar::addHeadlossFormulaDropdown()
{
    TopControlBarContent *bar_content = barContent(this->content);

    this->combo_headloss_formula = new ComboCheckboxes(this->content);
    this->combo_headloss_formula->setFixedWidth(190);

    this->combo_headloss_formula->addItem(
        QStringLiteral("Hazen-Williams"),
        static_cast<int>(HeadlossFormula::HazenWilliams),
        true,
        QStringLiteral("Run simulation with the Hazen-Williams headloss formula.<br><br>Requires pipe roughness coefficient C."));

    this->combo_headloss_formula->addItem(
        QStringLiteral("Darcy-Weisbach"),
        static_cast<int>(HeadlossFormula::DarcyWeisbach),
        false,
        QStringLiteral("Run simulation with the Darcy-Weisbach headloss formula.<br><br>Requires absolute pipe roughness ε in mm."));

    this->combo_headloss_formula->addItem(
        QStringLiteral("Chezy-Manning"),
        static_cast<int>(HeadlossFormula::ChezyManning),
        false,
        QStringLiteral("Run simulation with the Chezy-Manning headloss formula.<br><br>Requires Manning roughness coefficient n."));

    connect(this->combo_headloss_formula, &ComboCheckboxes::checkedItemsChanged, this, [this]
    {
        const QList<int> indexes_checked = this->combo_headloss_formula->checkedIndexes();
        HeadlossFormulas formulas = HeadlossFormula::None;

        for (const int index : indexes_checked)
        {
            const int value = this->combo_headloss_formula->itemData(index).toInt();
            const HeadlossFormula formula = static_cast<HeadlossFormula>(value);
            formulas |= formula;
        }

        emit signalHeadlossFormulaChanged(formulas);
    });

    bar_content->centerLayout()->addWidget(createLabeledControl(QStringLiteral("Headloss"), this->combo_headloss_formula, this->content));
}

void TopControlBar::addSimulationControls()
{
    TopControlBarContent *bar_content = barContent(this->content);
    const QSize button_size(40, Sizes::TopControlBarHeight - 10);

    QPushButton *button_sim_start = new QPushButton(this->content);
    button_sim_start->setIcon(QIcon(QStringLiteral(":/icon/simulation_start.png")));
    button_sim_start->setFlat(true);
    button_sim_start->setIconSize(QSize(30, 30));
    button_sim_start->setFixedSize(button_size);
    button_sim_start->setToolTip(QStringLiteral("Run Configured Simulations<br>[Ctrl]+[R]<br>[Shift]+[Enter]"));
    button_sim_start->addAction(QString(), QKeySequence(Qt::SHIFT | Qt::Key_Return), button_sim_start, &QPushButton::click);
    button_sim_start->addAction(QString(), QKeySequence(Qt::CTRL | Qt::Key_R), button_sim_start, &QPushButton::click);

    QPushButton *button_sim_log = new QPushButton(this->content);
    button_sim_log->setIcon(QIcon(QStringLiteral(":/icon/log.png")));
    button_sim_log->setFlat(true);
    button_sim_log->setIconSize(QSize(30, 30));
    button_sim_log->setFixedSize(button_size);
    button_sim_log->setToolTip(QStringLiteral("Show EPANET log<br>You need to run a simulation first"));
    button_sim_log->setEnabled(false);

    connect(button_sim_start, &QPushButton::clicked, this, [this, button_sim_log]
    {
        button_sim_log->setEnabled(true);
        emit signalSimulationStart();
    });

    connect(button_sim_log, &QPushButton::clicked, this, [this]
    {
        emit signalShowEpanetLog();
    });

    bar_content->centerLayout()->addWidget(button_sim_start);
    bar_content->centerLayout()->addWidget(button_sim_log);
}

void TopControlBar::addViewControls()
{
    TopControlBarContent *bar_content = barContent(this->content);
    bar_content->rightLayout()->addWidget(createSeparator(this->content));

    this->button_fullscreen = new QToolButton(this->content);
    this->button_fullscreen->setAutoRaise(true);
    this->button_fullscreen->setIconSize(QSize(30, 30));
    this->button_fullscreen->setFixedSize(40, Sizes::TopControlBarHeight - 10);

    connect(this->button_fullscreen, &QToolButton::clicked, this, [this]
    {
        emit signalFullScreenToggle();
    });

    setFullScreenState(false);
    bar_content->rightLayout()->addWidget(this->button_fullscreen);
}
