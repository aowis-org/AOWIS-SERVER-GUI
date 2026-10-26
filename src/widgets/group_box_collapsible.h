#ifndef GROUP_BOX_COLLAPSIBLE_H
#define GROUP_BOX_COLLAPSIBLE_H

#include <QGroupBox>

class QAbstractButton;
class QResizeEvent;

class GroupBoxCollapsible : public QGroupBox
{
    Q_OBJECT
    
public:
    explicit GroupBoxCollapsible(QWidget *parent = nullptr);
    explicit GroupBoxCollapsible(const QString &title, QWidget *parent = nullptr);
    
    void setCollapsed(bool collapsed);
    bool isCollapsed() const;
    // A compact action within the title bar, accessible even when collapsed.
    void setHeaderAction(QAbstractButton *button);
    
protected:
    void childEvent(QChildEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    
private:
    void applyExpandedState(bool expanded);
    QAbstractButton *header_action = nullptr;
    
signals:
    void signalCollapsed(GroupBoxCollapsible *group);
    void signalExpanded(GroupBoxCollapsible *group);
};

#endif // GROUP_BOX_COLLAPSIBLE_H
