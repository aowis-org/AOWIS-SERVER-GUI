#pragma once

// Reusable, GUI-independent ranked DAG layout kernel.
//
// An input graph contains nodes with stable IDs and parent edges. Roots form
// rank 0, and each descendant is assigned a strictly greater rank than its
// loaded parents. Barycentric sweeps minimize crossings; every rank is given
// distinct, non-overlapping horizontal slots. The caller is responsible for
// packing these *whole* DAG rectangles into its compound hierarchy: it must
// never interleave the contents of different child rectangles.
//
// This module intentionally has no Qt, Graphviz, subprocess, or platform
// dependencies, and can be compiled and tested independently of the GUI.

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace AowisGraphLayout
{
struct Vertex
{
    std::string id;
    std::vector<std::string> parents;
    int minimum_rank = 0;
};

struct Point
{
    double x = 0.0;
    double y = 0.0;
};

struct RankedDag
{
    std::vector<Point> positions; // In the same order as input vertices.
    std::vector<int> ranks;
    double width = 0.0;
    double height = 0.0;
};

// Node dimensions include their painted outline; pitch is the required
// separation of node centers. A narrow graph must still reserve one full
// card's width. All output positions are local, nonnegative, and centered in
// the measured graph rectangle.
inline RankedDag layoutRankedDag(const std::vector<Vertex> &vertices,
                                double node_width, double pitch_x, double pitch_y)
{
    RankedDag result;
    const int count = static_cast<int>(vertices.size());
    result.positions.resize(count);
    result.ranks.resize(count, -1);
    result.width = node_width;
    if (count == 0)
        return result;

    std::unordered_map<std::string, int> by_id;
    by_id.reserve(vertices.size());
    for (int i = 0; i < count; ++i)
        by_id.emplace(vertices[i].id, i);

    std::vector<std::vector<int>> parents(count);
    std::vector<std::vector<int>> children(count);
    for (int i = 0; i < count; ++i)
        for (const std::string &id : vertices[i].parents)
        {
            const std::unordered_map<std::string, int>::const_iterator found = by_id.find(id);
            if (found == by_id.end() || found->second == i)
                continue;
            parents[i].push_back(found->second);
            children[found->second].push_back(i);
        }

    std::vector<bool> visiting(count, false);
    std::function<int(int)> rankOf = [&](int i) -> int
    {
        if (result.ranks[i] >= 0)
            return result.ranks[i];
        // Ignore only the back-edge of a corrupt/cyclic history; never hang.
        if (visiting[i])
            return 0;
        visiting[i] = true;
        int rank = std::max(0, vertices[i].minimum_rank);
        for (int p : parents[i])
            if (!visiting[p])
                rank = std::max(rank, rankOf(p) + 1);
        visiting[i] = false;
        result.ranks[i] = rank;
        return rank;
    };

    int max_rank = 0;
    for (int i = 0; i < count; ++i)
        max_rank = std::max(max_rank, rankOf(i));

    // Discard any edge not strictly preceding its child. This can only occur
    // for malformed cyclic ancestry or a manually supplied minimum rank.
    for (int i = 0; i < count; ++i)
    {
        std::vector<int> valid_parents;
        for (int p : parents[i])
            if (result.ranks[p] < result.ranks[i])
                valid_parents.push_back(p);
        parents[i] = valid_parents;
    }
    children.assign(count, std::vector<int>());
    for (int i = 0; i < count; ++i)
        for (int p : parents[i])
            children[p].push_back(i);

    std::vector<std::vector<int>> layers(max_rank + 1);
    for (int i = 0; i < count; ++i)
        layers[result.ranks[i]].push_back(i);
    for (std::vector<int> &layer : layers)
        std::sort(layer.begin(), layer.end(), [&](int a, int b)
        {
            return vertices[a].id < vertices[b].id;
        });

    std::vector<double> order(count, 0.0);
    const std::function<void()> updateOrder = [&]()
    {
        for (const std::vector<int> &layer : layers)
            for (int slot = 0; slot < static_cast<int>(layer.size()); ++slot)
                order[layer[slot]] = slot - (layer.size() - 1) * 0.5;
    };
    updateOrder();

    // Crossing minimization is stable and input-order-independent. The
    // original horizontal order breaks equal-score ties to avoid oscillation.
    for (int sweep = 0; sweep < 12; ++sweep)
    {
        const bool downward = (sweep % 2 == 0);
        for (int r = 0; r < static_cast<int>(layers.size()); ++r)
        {
            const int rank = downward ? r : static_cast<int>(layers.size()) - r - 1;
            std::vector<int> &layer = layers[rank];
            const std::function<double(int)> score = [&](int i)
            {
                const std::vector<int> &adjacent = downward ? parents[i] : children[i];
                if (adjacent.empty())
                    return order[i];
                double sum = 0.0;
                for (int p : adjacent)
                    sum += order[p];
                return sum / adjacent.size();
            };
            std::stable_sort(layer.begin(), layer.end(), [&](int a, int b)
            {
                const double difference = score(a) - score(b);
                if (std::abs(difference) > 1e-8)
                    return difference < 0;
                return order[a] < order[b];
            });
            updateOrder();
        }
    }

    std::size_t maximum_layer_size = 1;
    for (const std::vector<int> &layer : layers)
        maximum_layer_size = std::max(maximum_layer_size, layer.size());
    result.width = node_width + (maximum_layer_size - 1) * pitch_x;
    result.height = max_rank * pitch_y;

    for (int rank = 0; rank <= max_rank; ++rank)
    {
        const std::vector<int> &layer = layers[rank];
        for (int slot = 0; slot < static_cast<int>(layer.size()); ++slot)
        {
            result.positions[layer[slot]].x = result.width / 2.0
                + (slot - (layer.size() - 1) * 0.5) * pitch_x;
            result.positions[layer[slot]].y = rank * pitch_y;
        }
    }
    return result;
}
}
