#include "graph/compound_dag_layout.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

using namespace AowisGraphLayout;

// CTest runs Release builds too: ordinary CHECK() would be compiled out.
#define CHECK(expression) do { \
    if (!(expression)) { \
        std::fprintf(stderr, "compound_dag_layout failed at line %d: %s\n", \
                     __LINE__, #expression); \
        return 1; \
    } \
} while (false)

int main()
{
    const RankedDag one = layoutRankedDag({{"root",{}}, {"head",{"root"}}, {"branch",{"head"}}}, 224,250,155);
    CHECK(one.width == 224);
    CHECK(one.positions[0].x == one.positions[1].x && one.positions[1].x == one.positions[2].x);
    CHECK(one.ranks[0] == 0 && one.ranks[1] == 1 && one.ranks[2] == 2);
    const std::vector<Vertex> multiple = {{"root",{}},{"head",{"root"}},
        {"b1",{"head"}}, {"b2",{"head"}}, {"b3",{"head"}}};
    const RankedDag a = layoutRankedDag(multiple, 224,250,155);
    CHECK(a.width == 724);
    CHECK(a.positions[0].x == a.positions[1].x);
    CHECK(a.positions[2].y == a.positions[3].y);
    CHECK(std::abs(a.positions[2].x-a.positions[3].x) >= 224);
    std::vector<Vertex> reversed = multiple;
    std::reverse(reversed.begin(), reversed.end());
    const RankedDag b = layoutRankedDag(reversed,224,250,155);
    for (size_t i=0;i<multiple.size();++i)
        for (size_t j=0;j<reversed.size();++j)
            if (multiple[i].id==reversed[j].id) CHECK(a.positions[i].x == b.positions[j].x && a.positions[i].y==b.positions[j].y);
    const RankedDag merge = layoutRankedDag({{"root",{}},{"left",{"root"}},{"right",{"root"}},{"merge",{"left","right"}}},224,250,155);
    CHECK(merge.ranks[3] == 2);
    CHECK(merge.positions[1].x != merge.positions[2].x);
    const RankedDag empty = layoutRankedDag({},224,250,155);
    CHECK(empty.positions.empty() && empty.width == 224);
    const RankedDag two_roots = layoutRankedDag({{"root-a",{}},{"root-b",{}},
        {"leaf-a",{"root-a"}},{"leaf-b",{"root-b"}}},224,250,155);
    CHECK(two_roots.positions[0].x < two_roots.positions[1].x);
    CHECK(two_roots.positions[2].x < two_roots.positions[3].x);
    // Every rank must be collision-free, even with many forks, merges,
    // and edges spanning several depth levels.
    std::vector<Vertex> wide;
    wide.push_back({"root", {}});
    for (int i = 0; i < 24; ++i)
        wide.push_back({"fork-" + std::to_string(i), {"root"}});
    for (int i = 0; i < 12; ++i)
        wide.push_back({"merge-" + std::to_string(i),
            {"fork-" + std::to_string(2*i), "fork-" + std::to_string(2*i+1)}});
    const RankedDag spaced = layoutRankedDag(wide,224,250,155);
    for (std::size_t i = 0; i < wide.size(); ++i)
        for (std::size_t j = i + 1; j < wide.size(); ++j)
        {
            const bool collision = std::abs(spaced.positions[i].x-spaced.positions[j].x) < 224
                && std::abs(spaced.positions[i].y-spaced.positions[j].y) < 84;
            CHECK(!collision);
        }
    const RankedDag cyclic = layoutRankedDag({{"a",{"b"}},{"b",{"a"}}},224,250,155);
    CHECK(cyclic.positions.size()==2);
}
