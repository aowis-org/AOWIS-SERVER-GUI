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
        nodes[database.node_index].position = QPointF(
            group_x + database.width / 2.0, 0.0);
        for (int pi = 0; pi < database.projects.size(); ++pi)
        {
            const ProjectBlock &project = database.projects.at(pi);
            const QPointF offset = database.project_offsets.at(pi);
            // Centre a single narrow project beneath its wider server card.
            const double project_x = group_x + offset.x()
                + (database.projects.size() == 1
                    ? (database.width - project.width) / 2.0 : 0.0);
            nodes[project.node_index].position = QPointF(
                project_x + project.width / 2.0, -project_header_y);
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
