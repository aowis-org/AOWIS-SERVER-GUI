#include "map_editor_guide_dock.h"

#include "../_sizes.h"

#include <QCloseEvent>
#include <QFrame>
#include <QTextBrowser>
#include <QTextDocument>

MapEditorGuideDock::MapEditorGuideDock(QWidget *parent)
    : QDockWidget("Map Editor Guide", parent)
{
    setMinimumWidth(Sizes::SidebarRightWidth);
    resize(Sizes::SidebarRightWidth, height());
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    QTextBrowser *guide = new QTextBrowser(this);
    guide->setReadOnly(true);
    guide->setOpenLinks(false);
    guide->setOpenExternalLinks(false);
    guide->setFrameShape(QFrame::NoFrame);
    guide->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    guide->setTextInteractionFlags(Qt::TextSelectableByMouse);
    guide->document()->setDocumentMargin(12.0);
    guide->setHtml(R"HTML(
        <h2>Using the Map Editor</h2>
        <p>
            AOWIS uses a deliberate editor-oriented mouse layout: the
            <b>left mouse button is reserved for navigation and selection</b>,
            while the <b>right mouse button performs editing actions</b>.
            This keeps map movement predictable and makes repeated network
            construction faster and less error-prone.
        </p>

        <h3>Navigate the map</h3>
        <ul>
            <li><b>Left-drag an empty map area:</b> pan the map.</li>
            <li><b>Mouse wheel:</b> zoom in or out around the pointer position.</li>
        </ul>

        <h3>Select network elements</h3>
        <ul>
            <li><b>Left-click an entity or link:</b> select it.</li>
            <li><b>Shift + left-click a node, pump, or valve:</b> add it to the current selection or remove it from the selection.</li>
            <li><b>Select tool + right-drag an empty map area:</b> draw a selection rectangle. Start away from pipes and entities; their right-click editing actions take priority. Pipes whose endpoints are selected are included automatically.</li>
            <li><b>Delete:</b> press <b>Del</b> or use <b>Delete Selected</b> in the editor toolbar.</li>
        </ul>

        <h3>Place nodes</h3>
        <ul>
            <li>Select <b>Junction</b>, <b>Tank</b>, or <b>Reservoir</b>.</li>
            <li><b>Right-click the map:</b> place the node at that position.</li>
            <li>The tool remains active so that several nodes can be placed quickly.</li>
        </ul>

        <h3>Draw pipes or cables</h3>
        <ul>
            <li>Select <b>Pipe / Cable</b>.</li>
            <li><b>Right-click a node connection point:</b> start the link. A valid target is highlighted.</li>
            <li><b>Right-click the map:</b> add intermediate bend points.</li>
            <li><b>Right-click a different node connection point:</b> finish the link.</li>
            <li>The tool is rearmed automatically for drawing the next link.</li>
        </ul>

        <h3>Place pumps and valves</h3>
        <ul>
            <li>Select <b>Pump</b> or <b>Valve / Switch</b>.</li>
            <li><b>Right-click the first node connection point</b>, then <b>right-click a different node connection point</b>.</li>
            <li>The device is inserted between the two nodes and the tool remains active for the next device.</li>
        </ul>

        <h3>Edit existing elements</h3>
        <ul>
            <li><b>Right-click a node, pump, or valve:</b> open its menu for moving or deleting it.</li>
            <li>When several entities are selected, right-click one of them and choose <b>Move selected entities</b> to move the complete selection together.</li>
            <li>After choosing a move action, move the pointer and <b>right-click to confirm the new position</b>.</li>
            <li><b>Right-click a pipe segment:</b> insert a new intermediate vertex at that position.</li>
            <li><b>Right-click an existing pipe vertex:</b> move it, delete it, or convert it to a junction.</li>
            <li><b>Convert to junction</b> creates a junction at the vertex and splits the original pipe into two connected pipes.</li>
        </ul>

        <h3>Finish or cancel a tool</h3>
        <p>
            Press <b>Esc</b> or choose <b>Select</b> to leave the current placement
            tool and return to selection mode.
        </p>
    )HTML");

    setWidget(guide);
}

void MapEditorGuideDock::setMapEditorActive(bool active)
{
    this->map_editor_active = active;
    updateVisibility();
}

void MapEditorGuideDock::setRequestedVisible(bool visible)
{
    this->requested_visible = visible;
    updateVisibility();
}

void MapEditorGuideDock::closeEvent(QCloseEvent *event)
{
    this->requested_visible = false;
    emit requestedVisibilityChanged(false);
    QDockWidget::closeEvent(event);
}

void MapEditorGuideDock::updateVisibility()
{
    setVisible(this->map_editor_active && this->requested_visible);
}
