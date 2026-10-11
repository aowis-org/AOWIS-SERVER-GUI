# Hydraulic table unit metadata

Each `TableColumn` declares its presentation metadata and filter kind explicitly.
The visible heading has no authority over quantities, conversion or filtering.

- `ColumnUnits::quantity` refers to the existing Model quantity registry. An empty
  identifier preserves a plain scalar or explicitly handled row-dependent value.
- `ColumnKind::EnergyPrice` selects the existing inverse-energy converter; currency
  is stored separately as an opaque label. Stored prices retain their per-kWh basis.
- `DemandDetails` marks compound text whose active filters need reevaluation after
  a unit change. Reaction kinds select order-dependent explanatory tooltips.
- Heterogeneous valve settings retain cell-level quantity identifiers. Reaction
  coefficients retain cell-level kind/order metadata.
- The quality-analysis enum selects the result column's heading and quantity
  together, including chemical concentration, age and source trace.
- `ColumnFilterKind` is set at the declaration, never inferred from a heading.

Cell presentation, headers, numeric filter refresh/reset and explicit filter edits
all use the metadata. `formattedTitle` may replace a trailing bracketed suffix for
presentation, but never interprets its contents. A heading without brackets receives
the selected unit suffix as well.

Sorting and numeric row filtering continue to use canonical `SortRole` values.
The table cells are read-only. Numeric filter bounds are converted back through
`toCanonicalForColumn` on edits; the untouched bound stays canonical. Unit refreshes
block control signals and retain canonical bounds.

When adding a column, declare its quantity (or special presentation kind) and filter
kind beside its heading. Use a Model registry identifier; do not add a table-specific
unit registry or derive behavior from localized text.

The existing `aowis-unit-table-presentation` test covers metadata-driven conversions,
localized/renamed headings, misleading suffixes, independent semantic profiles,
energy-price currency labels, repeated profile switching and filter-bound conversion.
It remains part of `aowis-tests`; no new executable is required.

## Numeric formatting and presentation policy

Table DisplayRole values are rendered on demand through `UnitTablePresentation`
using `NumericText` and `UnitPresentationContext`. The original per-column
fractional-digit count stays the default, including trailing-zero trimming and
the nonzero-small-value fallback. Locale and digit grouping always come from
`UnitPresentationContext`; an explicit per-quantity formatting override or a
changed global notation/precision takes priority over the column default.
Raw numeric cells without physical quantity metadata follow the same formatting
path, retaining their existing literal suffixes. This does not affect canonical
`SortRole` values, numerical filters, export semantics or Model values.

The simulation result presentation helpers and demand/reaction/energy-price
cells share this formatting path. When adding a new table column, record its
intended baseline decimal count as well as its physical quantity identifier.
The `aowis-unit-table-presentation` test covers German/English decimal marks,
grouping, overriding formatting policies, tiny values and canonical preservation.
