#ifndef MAP_EDITOR_GUIDE_DOCK_H
#define MAP_EDITOR_GUIDE_DOCK_H

#include <QDockWidget>

class QCloseEvent;

class MapEditorGuideDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit MapEditorGuideDock(QWidget *parent = nullptr);
    bool shouldBeVisible() const;

public slots:
    void setMapEditorActive(bool active);
    void setEditNetworkSectionActive(bool active);
    void setRequestedVisible(bool visible);

signals:
    void requestedVisibilityChanged(bool visible);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    bool map_editor_active = false;
    bool edit_network_section_active = true;
    bool requested_visible = true;

    void updateVisibility();
};

#endif // MAP_EDITOR_GUIDE_DOCK_H
