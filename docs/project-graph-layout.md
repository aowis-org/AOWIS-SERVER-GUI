# Projects graph placement

The Projects canvas displays a **compound DAG**, not a simple list of branch
names. Databases own projects; projects own immutable revisions; revisions link
to their actual parents. Only revision-to-revision edges represent ancestry.

## Current layout (deterministic, no new runtime dependency)

Implementation: `src/tabs/project_graph_placement.h`, called from
`ProjectsWidget::rebuildGraph()`.

1. **Layer the revisions by ancestry.** The rank of a revision is one greater
   than the maximum rank of its loaded parents. Disconnected roots may share
   rank zero; merges retain all their parent links. Broken/cyclic remote data
   cannot trap the layout in unbounded recursion.
2. **Order within ranks.** Start with a stable, identifier-based ordering;
   perform alternating forward and backward barycentre sweeps using parent and
   child locations. Each revision at a rank occupies a distinct horizontal
   slot, even if several report the same branch name. Historic branch labels
   never reserve permanent empty lanes.
3. **Measure and pack projects.** A project's width is determined by its
   busiest revision rank, not the number of branch names. Each project DAG is
   positioned within its own rectangular block. Blocks are packed into rows
   below their database header, using a bounded shelf width. Longer histories
   consume only that project's vertical extent.
4. **Pack databases.** A small dynamic program chooses breaks into horizontal
   rows that balance unused width, respecting source order. Rows are centred.
   The viewport aspect ratio informs the target width without making the
   layout depend on transient per-frame resizing.
5. **Route structural links.** Direct database/project links use curved
   connectors. If a project wraps to a lower shelf, the connector instead
   follows a gutter outside the database's project/revision cards. Real
   revision edges remain separate.

The graph widget maintains its existing top-down/bottom-up toggle and
independently folds ancestry in editable-head mode. The placement contract
keeps a single canonical coordinate system and leaves presentation flips to
the widget.

## Invariants covered by tests

- No two cards intersect, including unequal-length histories and wrapped rows.
- A loaded parent always precedes its descendant vertically.
- Same-rank revisions cannot overlap even with identical branch labels.
- Layout is deterministic when the API returns revisions in another order.
- Multiple database groups wrap into balanced rows.
- Server/project selection and folded-history navigation still work.

Run `./compile_linux_tests.sh` and inspect
`aowis-project-revision-graph`, `aowis-projects-inspector-dock`, and
`aowis-projects-multi-source` for regressions.

## Future scaling work

The initial patch improves placement but **does not** solve navigation of
hundreds or thousands of visible revision cards. Further iterations should
add viewport-aware collapse/expansion, spatial indexing and edge bundling,
then consider an established layout engine such as ELK for crossing reduction
on very dense merge graphs. Do not add an ELK/Graphviz dependency solely to
lay out server/project grouping: the compound hierarchy and UI interaction
still require application-specific handling.
