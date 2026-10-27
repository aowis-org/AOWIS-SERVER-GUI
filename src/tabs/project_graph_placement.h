#pragma once

#include "tabs/project_revision_graph.h"
#include "tabs/project_revision_card_geometry.h"
#include "graph/compound_dag_layout.h"

#include <QHash>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QVector>

#include <algorithm>
#include <cmath>

// Deterministic compound layout for Database -> Project -> oldest revision
// -> newest revision (HEAD). Branches are references attached at their HEAD
// rank, never ancestors or descendants of a revision.
//
// Positions use the graph's canonical bottom-up coordinates. The renderer
// mirrors them for the default top-down presentation. This algorithm does not
// create ancestry edges: it only positions the real nodes supplied by the GUI.
namespace ProjectGraphPlacement
{
constexpr double card_width = ProjectRevisionCardGeometry::width;
constexpr double revision_pitch_x = 250.0;
constexpr double revision_pitch_y = 155.0;
constexpr double project_gap_x = 48.0;
constexpr double database_gap_x = 84.0;
constexpr double project_header_y = 142.0;
constexpr double first_graph_layer_y = 286.0;

struct ProjectBlock
{
    int node_index = -1;
    QVector<int> revision_indices;
    QVector<QPointF> revision_positions;
    double width = card_width;
    double ancestry_center_x = card_width / 2.0;
};

struct DatabaseBlock
{
    int node_index = -1;
    QVector<ProjectBlock> projects;
    QVector<QPointF> project_offsets;
    double width = 252.0;
};

inline void layoutRevisions(ProjectBlock &block, const QVector<ProjectGraphNode> &nodes)
{
    // Layout revision history in chronological ancestry order. The persistent
    // 'parents' array already expresses oldest -> newest DAG dependencies;
    // it MUST NOT be reversed merely because a branch points at HEAD.
    std::vector<AowisGraphLayout::Vertex> vertices;
    vertices.reserve(std::size_t(block.revision_indices.size()));
    QHash<QString, int> revision_by_id;
    for (int i = 0; i < block.revision_indices.size(); ++i)
        revision_by_id.insert(nodes.at(block.revision_indices.at(i)).id, i);
    for (int index : block.revision_indices)
    {
        const ProjectGraphNode &revision = nodes.at(index);
        AowisGraphLayout::Vertex vertex;
        vertex.id = revision.id.toStdString();
        for (const QString &parent : revision.parents)
            if (revision_by_id.contains(parent))
                vertex.parents.push_back(parent.toStdString());
        vertices.push_back(vertex);
    }

    const AowisGraphLayout::RankedDag ranked =
        AowisGraphLayout::layoutRankedDag(vertices, card_width, revision_pitch_x,
                                         revision_pitch_y);
    const double ancestry_width = std::max(card_width, ranked.width);
    block.ancestry_center_x = ancestry_width / 2.0;
    block.revision_positions.resize(block.revision_indices.size());
    for (int i = 0; i < block.revision_indices.size(); ++i)
        block.revision_positions[i] = QPointF(ranked.positions[std::size_t(i)].x,
            first_graph_layer_y + ranked.positions[std::size_t(i)].y);

    // The extra height belongs to the immutable revision card itself: HEAD
    // badges are not independent layout vertices or side-lane cards. Grow the
    // rank separation if a revision carries several branch references.
    QHash<int, double> layer_height;
    int maximum_rank = 0;
    for (int i = 0; i < block.revision_indices.size(); ++i)
    {
        const int rank = ranked.ranks[std::size_t(i)];
        const ProjectGraphNode &node = nodes.at(block.revision_indices.at(i));
        const double height = ProjectRevisionCardGeometry::height(node.heads.size());
        layer_height[rank] = std::max(layer_height.value(rank, 84.0), height);
        maximum_rank = std::max(maximum_rank, rank);
    }
    QVector<double> layer_center(maximum_rank + 1, 0.0);
    for (int rank = 1; rank <= maximum_rank; ++rank)
        layer_center[rank] = layer_center[rank - 1]
            + std::max(revision_pitch_y, (layer_height.value(rank - 1, 84.0)
              + layer_height.value(rank, 84.0)) / 2.0 + 44.0);
    const double first_rank_y = std::max(first_graph_layer_y,
        project_header_y + (68.0 + layer_height.value(0, 84.0)) / 2.0 + 44.0);
    for (int i = 0; i < block.revision_indices.size(); ++i)
        block.revision_positions[i].setY(first_rank_y
            + layer_center.at(ranked.ranks[std::size_t(i)]));
}

inline void place(QVector<ProjectGraphNode> &nodes, double /*viewport_aspect*/ = 1.7)
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
        for (ProjectBlock &project : database.projects)
        {
            layoutRevisions(project, nodes);
            if (cursor_x > 0.0)
                cursor_x += project_gap_x;
            database.project_offsets.append(QPointF(cursor_x, 0.0));
            cursor_x += project.width;
        }
        database.width = std::max(252.0, cursor_x);
    }

    if (databases.isEmpty())
        return;

    // All database roots share one layer. Give each complete database group
    // its own horizontal corridor, then grow every project/revision subtree
    // downward from that common baseline. Never wrap database groups into
    // additional rows: a deep history must not push another server card down.
    double total_width = 0.0;
    for (const DatabaseBlock &database : databases)
        total_width += database.width;
    total_width += database_gap_x * (databases.size() - 1);

    double group_x = -total_width / 2.0;
    for (const DatabaseBlock &database : databases)
    {
        // Center the database over its project revision corridors.
        double left_project_x = 0.0;
        double right_project_x = 0.0;
        for (int pi = 0; pi < database.projects.size(); ++pi)
        {
            const double x = database.project_offsets.at(pi).x()
                + database.projects.at(pi).ancestry_center_x;
            if (pi == 0) left_project_x = x;
            right_project_x = x;
        }
        const double server_x = database.projects.isEmpty()
            ? database.width / 2.0 : (left_project_x + right_project_x) / 2.0;
        nodes[database.node_index].position = QPointF(group_x + server_x, 0.0);
        for (int pi = 0; pi < database.projects.size(); ++pi)
        {
            const ProjectBlock &project = database.projects.at(pi);
            const QPointF offset = database.project_offsets.at(pi);
            // Centre a single narrow project beneath its wider server card.
            const double project_x = group_x + offset.x()
                + (database.projects.size() == 1
                    ? (database.width - project.width) / 2.0 : 0.0);
            nodes[project.node_index].position = QPointF(
                project_x + project.ancestry_center_x, -project_header_y);
            for (int ri = 0; ri < project.revision_indices.size(); ++ri)
            {
                const QPointF position = project.revision_positions.at(ri);
                nodes[project.revision_indices.at(ri)].position = QPointF(
                    project_x + position.x(), -position.y());
            }
        }
        group_x += database.width + database_gap_x;
    }
}
}
