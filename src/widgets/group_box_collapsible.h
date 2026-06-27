#ifndef GROUP_BOX_COLLAPSIBLE_H
#define GROUP_BOX_COLLAPSIBLE_H

#include <QGroupBox>

class GroupBoxCollapsible : public QGroupBox
{
    Q_OBJECT
    
public:
    explicit GroupBoxCollapsible(QWidget *parent = nullptr);
    explicit GroupBoxCollapsible(const QString &title, QWidget *parent = nullptr);
    
    void setCollapsed(bool collapsed);
    bool isCollapsed() const;
    
protected:
    void childEvent(QChildEvent *event) override;
    
private:
    void applyExpandedState(bool expanded);
};

#endif // GROUP_BOX_COLLAPSIBLE_H
