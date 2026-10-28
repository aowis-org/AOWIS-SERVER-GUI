#pragma once

#include "tabs/project_revision_graph.h"

#include <QHash>
#include <QString>

// Every branch has exactly one *visual* incoming link. If its persisted
// creation-source branch is present, show that provenance link; otherwise
// attach the reference to its immutable HEAD revision. The HEAD UUID itself
// is never changed or inferred by this presentation rule.
namespace ProjectBranchVisualLink
{
inline QString parentId(const ProjectGraphNode &branch,
                        const QHash<QString, QString> &available_branch_projects)
{
    if (branch.kind != ProjectGraphNode::Kind::Branch)
        return {};
    if (!branch.origin_branch_node_id.isEmpty()
        && branch.origin_branch_node_id != branch.id
        && available_branch_projects.contains(branch.origin_branch_node_id)
        && available_branch_projects.value(branch.origin_branch_node_id)
            == branch.project_group_id)
        return branch.origin_branch_node_id;
    return branch.head_revision_id;
}
}
