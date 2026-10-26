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

// Deterministic compound layout for Database -> Project -> oldest revision
// -> newest revision (HEAD). Branches are references attached at their HEAD
// rank, never ancestors or descendants of a revision.
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
constexpr double project_header_y = 142.0;
constexpr double first_graph_layer_y = 286.0;
constexpr double branch_label_gap_x = 28.0;

struct ProjectBlock
{
    int node_index = -1;
    QVector<int> revision_indices;
    QVector<int> branch_indices;
    QVector<QPointF> branch_positions;
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

    // A branch is a named pointer, not part of the revision DAG. Keep its
    // clickable card next to its HEAD (at the *same* depth) in a reserved side
    // lane. Multiple aliases on one HEAD/rank receive independent slots.
    // Labels without a loaded HEAD remain accessible at the first history row.
    QHash<int, int> labels_per_rank;
    QHash<int, int> slot_per_rank;
    QVector<int> branch_ranks;
    branch_ranks.reserve(block.branch_indices.size());
    int maximum_labels = 0;
    for (int index : block.branch_indices)
    {
        const QString head_id = nodes.at(index).head_revision_id;
        const int revision_index = revision_by_id.value(head_id, -1);
        const int rank = revision_index >= 0
            ? ranked.ranks[std::size_t(revision_index)] : 0;
        branch_ranks.append(rank);
        const int count = ++labels_per_rank[rank];
        maximum_labels = std::max(maximum_labels, count);
    }
    block.width = ancestry_width;
    if (maximum_labels > 0)
        block.width += branch_label_gap_x + card_width
            + (maximum_labels - 1) * revision_pitch_x;
    block.branch_positions.resize(block.branch_indices.size());
    for (int i = 0; i < block.branch_indices.size(); ++i)
    {
        const int rank = branch_ranks.at(i);
        const int slot = slot_per_rank[rank]++;
        block.branch_positions[i] = QPointF(
            ancestry_width + branch_label_gap_x + card_width / 2.0
                + slot * revision_pitch_x,
            first_graph_layer_y + rank * revision_pitch_y);
    }
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
        // Centre the database over its *project roots*, not over the branch
        // label side lanes, so straight ancestry chains remain straight.
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
            for (int bi = 0; bi < project.branch_indices.size(); ++bi)
            {
                const QPointF position = project.branch_positions.at(bi);
                nodes[project.branch_indices.at(bi)].position = QPointF(
                    project_x + position.x(), -position.y());
            }
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
