#pragma once

#include "tabs/project_revision_graph.h"

#include <QHash>
#include <QPointF>
#include <QString>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

// Deterministic, compound layout for Database -> Project -> revision DAG.
//
// Positions use the graph's canonical bottom-up coordinates. The renderer
// mirrors them for the default top-down presentation. This algorithm does not
// create ancestry edges: it only positions the real nodes supplied by the GUI.
namespace ProjectGraphPlacement
{
constexpr double card_width = 224.0;
constexpr double revision_pitch_x = 250.0;
constexpr double revision_pitch_y = 155.0;
constexpr double project_gap_x = 48.0;
constexpr double project_gap_y = 80.0;
constexpr double database_gap_x = 84.0;
constexpr double database_gap_y = 92.0;
constexpr double project_header_y = 142.0;
constexpr double first_revision_y = 286.0;

struct ProjectBlock
{
    int node_index = -1;
    QVector<int> revision_indices;
    QVector<QPointF> revision_positions;
    double width = card_width;
    double height = first_revision_y + 55.0;
};

struct DatabaseBlock
{
    int node_index = -1;
    QVector<ProjectBlock> projects;
    QVector<QPointF> project_offsets;
    double width = 252.0;
    double height = project_header_y + 70.0;
};

inline void layoutRevisions(ProjectBlock &block, const QVector<ProjectGraphNode> &nodes)
{
    const int count = block.revision_indices.size();
    if (count == 0)
        return;

    QHash<QString, int> revision_index;
    for (int index = 0; index < count; ++index)
        revision_index.insert(nodes.at(block.revision_indices.at(index)).id, index);

    QVector<int> depths(count, -1);
    QVector<bool> visiting(count, false);
    std::function<int(int)> depthOf = [&](int index) -> int
    {
        if (depths.at(index) >= 0)
            return depths.at(index);
        if (visiting.at(index))
            return 0; // tolerate malformed remote ancestry without recursion loops
        visiting[index] = true;
        int depth = 0;
        const ProjectGraphNode &node = nodes.at(block.revision_indices.at(index));
        for (const QString &parent_id : node.parents)
        {
            const int parent_index = revision_index.value(parent_id, -1);
            if (parent_index >= 0 && parent_index != index)
                depth = std::max(depth, depthOf(parent_index) + 1);
        }
        visiting[index] = false;
        depths[index] = depth;
        return depth;
    };

    int max_depth = 0;
    for (int index = 0; index < count; ++index)
        max_depth = std::max(max_depth, depthOf(index));

    QVector<QVector<int>> layers(max_depth + 1);
    for (int index = 0; index < count; ++index)
        layers[depths.at(index)].append(index);

    // Deterministic starting order: a fetch's arrival order must not cause
    // unrelated nodes to jump after catalogue refresh.
    for (QVector<int> &layer : layers)
        std::sort(layer.begin(), layer.end(), [&](int lhs, int rhs)
        {
            return nodes.at(block.revision_indices.at(lhs)).id
                < nodes.at(block.revision_indices.at(rhs)).id;
        });

    QVector<QVector<int>> parents(count);
    QVector<QVector<int>> children(count);
    for (int index = 0; index < count; ++index)
    {
        const ProjectGraphNode &node = nodes.at(block.revision_indices.at(index));
        for (const QString &parent_id : node.parents)
        {
            const int parent_index = revision_index.value(parent_id, -1);
            if (parent_index < 0 || parent_index == index
                || depths.at(parent_index) >= depths.at(index))
                continue;
            parents[index].append(parent_index);
            children[parent_index].append(index);
        }
    }

    QVector<double> order(count, 0.0);
    const auto updateOrders = [&]()
    {
        for (const QVector<int> &layer : layers)
            for (int rank = 0; rank < layer.size(); ++rank)
                order[layer.at(rank)] = rank - (layer.size() - 1) / 2.0;
    };
    updateOrders();

    // Sugiyama-style alternating median/barycentre sweeps. Each depth has a
    // unique slot, so revisions at one depth can never overlap. Parents and
    // children pull nearby nodes toward each other, reducing crossings.
    for (int sweep = 0; sweep < 6; ++sweep)
    {
        const bool forward = (sweep % 2 == 0);
        for (int offset = 0; offset < layers.size(); ++offset)
        {
            const int depth = forward ? offset : layers.size() - offset - 1;
            QVector<int> &layer = layers[depth];
            const auto scoreOf = [&](int index)
            {
                const QVector<int> &neighbours = forward ? parents.at(index) : children.at(index);
                if (neighbours.isEmpty())
                    return order.at(index);
                double sum = 0.0;
                for (int neighbour : neighbours)
                    sum += order.at(neighbour);
                return sum / neighbours.size();
            };
            // Preserve previous order as a tie-breaker to prevent layout jitter.
            std::stable_sort(layer.begin(), layer.end(), [&](int lhs, int rhs)
            {
                const double lhs_score = scoreOf(lhs);
                const double rhs_score = scoreOf(rhs);
                if (std::abs(lhs_score - rhs_score) > 0.000001)
                    return lhs_score < rhs_score;
                return order.at(lhs) < order.at(rhs);
            });
            updateOrders();
        }
    }

    int maximum_layer_width = 1;
    for (const QVector<int> &layer : layers)
        maximum_layer_width = std::max(maximum_layer_width, int(layer.size()));
    block.width = card_width + (maximum_layer_width - 1) * revision_pitch_x;
    block.height = first_revision_y + max_depth * revision_pitch_y + 54.0;
    block.revision_positions.resize(count);
    for (int depth = 0; depth < layers.size(); ++depth)
    {
        const QVector<int> &layer = layers.at(depth);
        for (int slot = 0; slot < layer.size(); ++slot)
        {
            const double centered_slot = slot - (layer.size() - 1) / 2.0;
            block.revision_positions[layer.at(slot)] = QPointF(
                block.width / 2.0 + centered_slot * revision_pitch_x,
                first_revision_y + depth * revision_pitch_y);
        }
    }
}

inline void place(QVector<ProjectGraphNode> &nodes, double viewport_aspect = 1.7)
{
    QVector<DatabaseBlock> databases;
    QHash<QString, int> database_index;
    QHash<QString, QPair<int, int>> project_index;

    for (int index = 0; index < nodes.size(); ++index)
    {
        const ProjectGraphNode &node = nodes.at(index);
        if (node.kind == ProjectGraphNode::Kind::Server)
        {
            database_index.insert(node.id, databases.size());
            DatabaseBlock database;
            database.node_index = index;
            databases.append(database);
        }
    }
    for (int index = 0; index < nodes.size(); ++index)
    {
        const ProjectGraphNode &node = nodes.at(index);
        if (node.kind != ProjectGraphNode::Kind::Project)
            continue;
        const int db = database_index.value(node.container_id, -1);
        if (db < 0)
            continue;
        ProjectBlock project;
        project.node_index = index;
        project_index.insert(node.id, qMakePair(db, databases.at(db).projects.size()));
        databases[db].projects.append(project);
    }
    for (int index = 0; index < nodes.size(); ++index)
    {
        const ProjectGraphNode &node = nodes.at(index);
        if (node.kind != ProjectGraphNode::Kind::Revision)
            continue;
        const QPair<int, int> match = project_index.value(node.project_group_id,
                                                          qMakePair(-1, -1));
        if (match.first >= 0)
            databases[match.first].projects[match.second].revision_indices.append(index);
    }

    // Pack complete project DAGs into shelves within their owning database.
    // A long history consumes height, not unused horizontal branch lanes.
    constexpr double max_project_shelf_width = 1170.0;
    for (DatabaseBlock &database : databases)
    {
        double cursor_x = 0.0;
        double cursor_y = 0.0;
        double shelf_height = 0.0;
        double used_width = 0.0;
        for (ProjectBlock &project : database.projects)
        {
            layoutRevisions(project, nodes);
            if (cursor_x > 0.0
                && cursor_x + project_gap_x + project.width > max_project_shelf_width)
            {
                cursor_y += shelf_height + project_gap_y;
                cursor_x = 0.0;
                shelf_height = 0.0;
            }
            if (cursor_x > 0.0)
                cursor_x += project_gap_x;
            database.project_offsets.append(QPointF(cursor_x, cursor_y));
            cursor_x += project.width;
            used_width = std::max(used_width, cursor_x);
            shelf_height = std::max(shelf_height, project.height);
        }
        database.width = std::max(252.0, used_width);
        database.height = project_header_y + cursor_y + shelf_height;
        if (database.projects.isEmpty())
            database.height = project_header_y;
    }

    if (databases.isEmpty())
        return;

    // Choose balanced outer shelves rather than a single endlessly widening
    // strip. The DP discourages both excessive whitespace and orphaned last
    // rows while preserving the sidebar's stable database order.
    viewport_aspect = std::max(0.7, std::min(2.7, viewport_aspect));
    const double target_width = std::max(830.0,
        std::min(1520.0, 990.0 * std::sqrt(viewport_aspect / 1.4)));
    const int count = databases.size();
    QVector<double> best(count + 1, std::numeric_limits<double>::infinity());
    QVector<int> previous(count + 1, -1);
    best[0] = 0.0;
    for (int first = 0; first < count; ++first)
    {
        if (!std::isfinite(best.at(first)))
            continue;
        double width = 0.0;
        for (int last = first; last < count; ++last)
        {
            width += (last == first ? 0.0 : database_gap_x) + databases.at(last).width;
            if (width > target_width && last > first)
                break;
            const double spare = std::max(0.0, target_width - width) / target_width;
            const double cost = best.at(first) + 0.22 + spare * spare;
            if (cost < best.at(last + 1))
            {
                best[last + 1] = cost;
                previous[last + 1] = first;
            }
        }
    }
    QVector<QPair<int, int>> rows;
    for (int end = count; end > 0; end = previous.at(end))
    {
        const int begin = previous.at(end);
        if (begin < 0)
            return;
        rows.prepend(qMakePair(begin, end));
    }

    double row_y = 0.0;
    for (const QPair<int, int> &row : rows)
    {
        double row_width = 0.0;
        double row_height = 0.0;
        for (int index = row.first; index < row.second; ++index)
        {
            row_width += (index == row.first ? 0.0 : database_gap_x)
                         + databases.at(index).width;
            row_height = std::max(row_height, databases.at(index).height);
        }
        double row_x = -row_width / 2.0;
        for (int index = row.first; index < row.second; ++index)
        {
            const DatabaseBlock &database = databases.at(index);
            if (index != row.first)
                row_x += database_gap_x;
            const double center_x = row_x + database.width / 2.0;
            nodes[database.node_index].position = QPointF(center_x, -row_y);
            for (int pi = 0; pi < database.projects.size(); ++pi)
            {
                const ProjectBlock &project = database.projects.at(pi);
                const QPointF offset = database.project_offsets.at(pi);
                const double project_x = row_x + offset.x();
                const double project_y = row_y + project_header_y + offset.y();
                nodes[project.node_index].position = QPointF(
                    project_x + project.width / 2.0, -project_y);
                for (int ri = 0; ri < project.revision_indices.size(); ++ri)
                {
                    const QPointF position = project.revision_positions.at(ri);
                    nodes[project.revision_indices.at(ri)].position = QPointF(
                        project_x + position.x(), -(row_y + offset.y() + position.y()));
                }
            }
            row_x += database.width;
        }
        row_y += row_height + database_gap_y;
    }
}
}
