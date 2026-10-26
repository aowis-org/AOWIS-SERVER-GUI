#pragma once

#include "tabs/project_revision_graph.h"
#include "graph/compound_dag_layout.h"

#include <QHash>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QVector>

#include <algorithm>
#include <cmath>
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
constexpr double database_gap_x = 84.0;
constexpr double database_gap_y = 92.0;
constexpr double project_header_y = 142.0;
constexpr double first_revision_y = 286.0;

struct ProjectBlock
{
    int node_index = -1;
    QVector<int> revision_indices;
    QVector<int> branch_indices;
    QVector<QPointF> branch_positions;
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
    // Layout a project as one indivisible compound DAG. Revision ancestry and
    // branch-to-HEAD annotations are separate edges, but both participate in
    // rank assignment and crossing reduction. Branches whose HEAD has not
    // been loaded remain visible on a final fallback rank.
    std::vector<AowisGraphLayout::Vertex> vertices;
    vertices.reserve(std::size_t(block.revision_indices.size() + block.branch_indices.size()));
    for (int index : block.revision_indices)
    {
        const ProjectGraphNode &node = nodes.at(index);
        AowisGraphLayout::Vertex vertex;
        vertex.id = node.id.toStdString();
        for (const QString &parent : node.parents)
            vertex.parents.push_back(parent.toStdString());
        vertices.push_back(vertex);
    }

    const AowisGraphLayout::RankedDag revision_layout =
        AowisGraphLayout::layoutRankedDag(vertices, card_width, revision_pitch_x,
                                         revision_pitch_y);
    int fallback_rank = 0;
    for (int rank : revision_layout.ranks)
        fallback_rank = std::max(fallback_rank, rank + 1);

    QSet<QString> loaded_revisions;
    for (int index : block.revision_indices)
        loaded_revisions.insert(nodes.at(index).id);
    for (int index : block.branch_indices)
    {
        const ProjectGraphNode &node = nodes.at(index);
        AowisGraphLayout::Vertex vertex;
        vertex.id = node.id.toStdString();
        // A branch label occupies the layer immediately beneath the HEAD
        // (not an unrelated bottom row), respecting the project DAG.
        const bool head_loaded = loaded_revisions.contains(node.head_revision_id);
        if (head_loaded)
            vertex.parents.push_back(node.head_revision_id.toStdString());
        vertex.minimum_rank = head_loaded ? 0 : fallback_rank;
        vertices.push_back(vertex);
    }

    const AowisGraphLayout::RankedDag ranked =
        AowisGraphLayout::layoutRankedDag(vertices, card_width, revision_pitch_x,
                                         revision_pitch_y);
    block.width = std::max(card_width, ranked.width);
    block.revision_positions.resize(block.revision_indices.size());
    for (int i = 0; i < block.revision_indices.size(); ++i)
        block.revision_positions[i] = QPointF(ranked.positions[std::size_t(i)].x,
            first_revision_y + ranked.positions[std::size_t(i)].y);
    block.branch_positions.resize(block.branch_indices.size());
    for (int i = 0; i < block.branch_indices.size(); ++i)
    {
        const std::size_t index = std::size_t(block.revision_indices.size() + i);
        block.branch_positions[i] = QPointF(ranked.positions[index].x,
            first_revision_y + ranked.positions[index].y);
    }
    block.height = vertices.empty() ? project_header_y + 70.0
        : first_revision_y + ranked.height + 70.0;
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
        if (node.kind != ProjectGraphNode::Kind::Branch)
            continue;
        const QPair<int, int> match = project_index.value(node.container_id, qMakePair(-1, -1));
        if (match.first >= 0)
            databases[match.first].projects[match.second].branch_indices.append(index);
    }
    // Stable branch-card order even when the backend returns branches in a
    // different order on the next catalogue refresh.
    for (DatabaseBlock &database : databases)
        for (ProjectBlock &project : database.projects)
            std::sort(project.branch_indices.begin(), project.branch_indices.end(),
                [&](int lhs, int rhs) { return nodes.at(lhs).id < nodes.at(rhs).id; });
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

    // Stable project order across catalogue refreshes irrespective of backend
    // response ordering. All branch/revision indices have already been bound
    // to their owning ProjectBlock, so the blocks can now be reordered safely.
    for (DatabaseBlock &database : databases)
        std::sort(database.projects.begin(), database.projects.end(),
            [&](const ProjectBlock &lhs, const ProjectBlock &rhs)
            {
                return nodes.at(lhs.node_index).id < nodes.at(rhs.node_index).id;
            });

    // A database is a strict compound parent. Its projects are SIBLINGS in
    // one rank, with mutually disjoint x corridors for every descendant.
    // Never wrap projects of the same server into vertical shelves: a server
    // -> project edge would necessarily cross another project's subtree.
    // Wrapping happens only at the outer database-group boundary.
    for (DatabaseBlock &database : databases)
    {
        double cursor_x = 0.0;
        double tallest_project = 0.0;
        for (ProjectBlock &project : database.projects)
        {
            layoutRevisions(project, nodes);
            if (cursor_x > 0.0)
                cursor_x += project_gap_x;
            database.project_offsets.append(QPointF(cursor_x, 0.0));
            cursor_x += project.width;
            tallest_project = std::max(tallest_project, project.height);
        }
        database.width = std::max(252.0, cursor_x);
        database.height = project_header_y + tallest_project;
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
                // A narrow one-project corridor is centered beneath the
                // wider server card; wider groups retain their own origin.
                const double used_width = database.projects.size() == 1
                    ? project.width : database.width;
                const double project_x = row_x + offset.x()
                    + (database.width - used_width) / 2.0;
                const double project_y = row_y + project_header_y + offset.y();
                nodes[project.node_index].position = QPointF(
                    project_x + project.width / 2.0, -project_y);
                for (int bi = 0; bi < project.branch_indices.size(); ++bi)
                {
                    const QPointF position = project.branch_positions.at(bi);
                    nodes[project.branch_indices.at(bi)].position = QPointF(
                        project_x + position.x(), -(row_y + offset.y() + position.y()));
                }
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
