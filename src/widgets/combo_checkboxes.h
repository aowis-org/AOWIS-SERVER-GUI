#ifndef COMBO_CHECKBOXES_H
#define COMBO_CHECKBOXES_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QMenu;
class QPushButton;
class QWidgetAction;

class ComboCheckboxes : public QWidget
{
    Q_OBJECT
    
public:
    explicit ComboCheckboxes(QWidget *parent = nullptr);
    
    void addItem(const QString &text,
                 const QVariant &data = QVariant(),
                 bool checked = false);
    void insertItem(int index,
                    const QString &text,
                    const QVariant &data = QVariant(),
                    bool checked = false);
    void clear();
    
    int count() const;
    
    QString itemText(int index) const;
    void setItemText(int index, const QString &text);
    
    QVariant itemData(int index) const;
    void setItemData(int index, const QVariant &data);
    
    bool isItemChecked(int index) const;
    void setItemChecked(int index, bool checked);
    
    QStringList checkedTexts() const;
    QList<QVariant> checkedData() const;
    QList<int> checkedIndexes() const;
    
    QString placeholderText() const;
    void setPlaceholderText(const QString &text);
    
    int summaryLimit() const;
    void setSummaryLimit(int limit);
    
    QSize sizeHint() const override;
    
signals:
    void itemCheckedChanged(int index, bool checked);
    void checkedItemsChanged();
    
private:
    struct Item
    {
        QString text;
        QVariant data;
        QCheckBox *check_box = nullptr;
        QWidgetAction *action = nullptr;
    };
    
    QPushButton *button = nullptr;
    QMenu *menu = nullptr;
    QVector<Item> items;
    
    QString placeholder_text = QStringLiteral("Select...");
    int summary_limit = 3;
    
    bool hasValidIndex(int index) const;
    int indexOfCheckBox(const QCheckBox *check_box) const;
    void updateSummaryText();
};

#endif // COMBO_CHECKBOXES_H
