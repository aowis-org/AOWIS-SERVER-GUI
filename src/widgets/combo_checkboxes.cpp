#include "combo_checkboxes.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QSizePolicy>
#include <QWidgetAction>

ComboCheckboxes::ComboCheckboxes(QWidget *parent)
    : QWidget(parent),
    button(new QPushButton(this)),
    menu(new QMenu(this))
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    this->button->setMenu(this->menu);
    this->button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    this->button->setText(this->placeholder_text);
    
    layout->addWidget(this->button);
    
    updateSummaryText();
}

void ComboCheckboxes::addItem(const QString &text,
                              const QVariant &data,
                              bool checked)
{
    insertItem(this->items.count(), text, data, checked);
}

void ComboCheckboxes::insertItem(int index,
                                 const QString &text,
                                 const QVariant &data,
                                 bool checked)
{
    if (index < 0)
    {
        index = 0;
    }
    
    if (index > this->items.count())
    {
        index = this->items.count();
    }
    
    QWidget *row_widget = new QWidget(this->menu);
    QHBoxLayout *row_layout = new QHBoxLayout(row_widget);
    row_layout->setContentsMargins(8, 4, 8, 4);
    row_layout->setSpacing(0);
    
    QCheckBox *check_box = new QCheckBox(text, row_widget);
    check_box->setChecked(checked);
    check_box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    row_layout->addWidget(check_box);
    
    QWidgetAction *action = new QWidgetAction(this->menu);
    action->setDefaultWidget(row_widget);
    
    if (index < this->items.count())
    {
        this->menu->insertAction(this->items.at(index).action, action);
    }
    else
    {
        this->menu->addAction(action);
    }
    
    Item item;
    item.text = text;
    item.data = data;
    item.check_box = check_box;
    item.action = action;
    
    this->items.insert(index, item);
    
    connect(check_box, &QCheckBox::toggled,
            this,
            [this, check_box](bool checked)
            {
                const int changed_index = indexOfCheckBox(check_box);
                
                if (changed_index < 0)
                {
                    return;
                }
                
                updateSummaryText();
                emit itemCheckedChanged(changed_index, checked);
                emit checkedItemsChanged();
            });
    
    updateSummaryText();
}

void ComboCheckboxes::clear()
{
    this->menu->clear();
    this->items.clear();
    
    updateSummaryText();
}

int ComboCheckboxes::count() const
{
    return this->items.count();
}

QString ComboCheckboxes::itemText(int index) const
{
    if (!hasValidIndex(index))
    {
        return QString();
    }
    
    return this->items.at(index).text;
}

void ComboCheckboxes::setItemText(int index, const QString &text)
{
    if (!hasValidIndex(index))
    {
        return;
    }
    
    this->items[index].text = text;
    
    if (this->items.at(index).check_box != nullptr)
    {
        this->items.at(index).check_box->setText(text);
    }
    
    updateSummaryText();
}

QVariant ComboCheckboxes::itemData(int index) const
{
    if (!hasValidIndex(index))
    {
        return QVariant();
    }
    
    return this->items.at(index).data;
}

void ComboCheckboxes::setItemData(int index, const QVariant &data)
{
    if (!hasValidIndex(index))
    {
        return;
    }
    
    this->items[index].data = data;
}

bool ComboCheckboxes::isItemChecked(int index) const
{
    if (!hasValidIndex(index))
    {
        return false;
    }
    
    const QCheckBox *check_box = this->items.at(index).check_box;
    
    return check_box != nullptr && check_box->isChecked();
}

void ComboCheckboxes::setItemChecked(int index, bool checked)
{
    if (!hasValidIndex(index))
    {
        return;
    }
    
    QCheckBox *check_box = this->items.at(index).check_box;
    
    if (check_box == nullptr)
    {
        return;
    }
    
    check_box->setChecked(checked);
    updateSummaryText();
}

QStringList ComboCheckboxes::checkedTexts() const
{
    QStringList result;
    
    for (int index = 0; index < this->items.count(); ++index)
    {
        if (isItemChecked(index))
        {
            result << this->items.at(index).text;
        }
    }
    
    return result;
}

QList<QVariant> ComboCheckboxes::checkedData() const
{
    QList<QVariant> result;
    
    for (int index = 0; index < this->items.count(); ++index)
    {
        if (isItemChecked(index))
        {
            result << this->items.at(index).data;
        }
    }
    
    return result;
}

QList<int> ComboCheckboxes::checkedIndexes() const
{
    QList<int> result;
    
    for (int index = 0; index < this->items.count(); ++index)
    {
        if (isItemChecked(index))
        {
            result << index;
        }
    }
    
    return result;
}

QString ComboCheckboxes::placeholderText() const
{
    return this->placeholder_text;
}

void ComboCheckboxes::setPlaceholderText(const QString &text)
{
    this->placeholder_text = text;
    updateSummaryText();
}

int ComboCheckboxes::summaryLimit() const
{
    return this->summary_limit;
}

void ComboCheckboxes::setSummaryLimit(int limit)
{
    if (limit < 1)
    {
        limit = 1;
    }
    
    this->summary_limit = limit;
    updateSummaryText();
}

QSize ComboCheckboxes::sizeHint() const
{
    return this->button->sizeHint();
}

bool ComboCheckboxes::hasValidIndex(int index) const
{
    return index >= 0 && index < this->items.count();
}

int ComboCheckboxes::indexOfCheckBox(const QCheckBox *check_box) const
{
    for (int index = 0; index < this->items.count(); ++index)
    {
        if (this->items.at(index).check_box == check_box)
        {
            return index;
        }
    }
    
    return -1;
}

void ComboCheckboxes::updateSummaryText()
{
    QStringList selected = checkedTexts();
    
    if (selected.isEmpty())
    {
        this->button->setText(this->placeholder_text);
        return;
    }
    
    if (selected.count() <= this->summary_limit)
    {
        this->button->setText(selected.join(QStringLiteral(", ")));
        return;
    }
    
    QStringList visible_items;
    
    for (int index = 0; index < this->summary_limit; ++index)
    {
        visible_items << selected.at(index);
    }
    
    const int hidden_count = selected.count() - this->summary_limit;
    const QString text = QStringLiteral("%1 +%2")
                             .arg(visible_items.join(QStringLiteral(", ")))
                             .arg(hidden_count);
    
    this->button->setText(text);
}
