# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added 

- `add_component()` to build an InvestmentBlock as a weighted sum of several
  InvestmentFunction components, each exposed separately to BundleSolver (the
  disaggregated path); the single-component (legacy) path is unchanged.

- netCDF (1:K) format for the disaggregated InvestmentBlock: the group may now
  hold one `Component_<k>` sub-group per component (the presence of
  `Component_0` selects the disaggregated path; the component count is
  implicit in the suffixes), each a legacy InvestmentFunction description
  with its own `Weight` attribute and, for the multi-period case, its own
  `AssetVarIndex` and `AssetBaselineVarIndex` variables; all variable indices
  in the file are GLOBAL (positions in the design array of the root); both
  deserialize and serialize support it, and the legacy single-component
  format is read and written unchanged

- `AssetBaselineVarIndex` (netCDF) and
  `set_asset_baseline_variable_indices()`: per-asset *variable* baseline for
  the multi-period case — the transition cost of asset i is charged against
  the value of another design variable (typically the same asset's variable
  in the previous period) instead of the `InstalledQuantity` datum, with the
  mirrored subgradient entry on the baseline variable; cannot be combined
  with `InstalledQuantity`

- `add_component( f , weight , actives )` overload binding a component to a
  SUBSET of the design variables (per-period binding; the component's
  mappings are then local positions in that subset)

- automatic active-subset derivation in deserialize: each component is wired
  to the union of the design variables its mappings reference (sorted, so
  the BundleSolver increasing-union-order rule holds), with the mappings
  translated from global to local; identity/full components keep the dense
  wiring byte-identically. Serialize performs the inverse local→global
  re-translation, so files always speak global indices

- `fix_design_variable()` on InvestmentBlock (fix/unfix one design variable,
  with Modification)

- StochasticBlock components: an `InnerBlock` group that is a `StochasticBlock`
  is a template that deserialize expands into one component per scenario of its
  ScenarioGenerator (looked up in the component first, then at the root), each
  carrying its own inner Block materialized on that scenario and weighted by
  (scenario probability) x (`Weight`). This works both inside a `Component_<k>`
  and as the InvestmentBlock's direct inner (no `Component_0`) — the two speak
  the same template format and expand identically. A StochasticBlock without a
  ScenarioGenerator, and a ScenarioGenerator with no StochasticBlock to expand
  (in either the disaggregated or the legacy branch), are both rejected

- `InvestmentFunction::deserialize( group , inner )`, which takes the inner
  Block from outside instead of creating it out of the `InnerBlock` group
  (this is what the expansion above hands each scenario component)

- `InvestmentBlockSolution` now carries one inner Solution per component,
  serialized as `InnerSolution_<k>` groups with a `NumInnerSolutions`
  round-trip guardrail; the legacy single-`InnerSolution` format is unchanged

- `set_weight()` on InvestmentFunction, to weight a component (> 0)

- `set_asset_variable_indices()` on InvestmentFunction, mapping each asset to the
  design variable it invests in, so several components can share the same design
  variables while each one acts on its own subset of them (the multi-period case)

- `set_implicit_constraints()` on InvestmentFunction, to set the implicit
  linear constraints (caps or budgets over the assets of the component)
  programmatically; one coefficient per ASSET, as the netCDF format declares

- `set_variable_bounds()` on InvestmentBlock, plus accessors for the
  programmatic construction path

### Changed 

- `generate_objective()` now builds a disaggregated sum when there are multiple
  components; the single-component (legacy) path is unchanged

- the `f_reformulate_bounds` option cannot be combined with a multi-component
  construction: it is now rejected with an error rather than silently producing
  wrong results

- the deserialize consistency check between the number of assets and of
  active variables is now mapping-aware (a component with an `AssetVarIndex`
  may invest in a subset of the design variables)

- the implicit constraints now follow the PER-ASSET semantics the netCDF
  format declares (`Constraints_A` is NumConstraints × NumAssets): column j
  applies to the variable of asset j through the mapping — in constraint
  evaluation and in the vertical linearizations alike; under the legacy
  identity mapping the behaviour is bit-identical

- `Cost` and `DisinvestmentCost` must be >= 0 (convexity precondition of the
  transition cost): enforced with an error at deserialize

- `add_component()` wires the active variables itself when the component has
  none (all the design variables, in natural order); `is_disaggregated()` is
  now structural (`get_number_nested_Blocks() > 0`)

### Removed

- the optional `NumComponents` dimension (the component count is implicit in
  the `Component_<k>` suffixes)

- `get_variables_writable()` (superseded by `fix_design_variable()` and by
  `add_component()` wiring the actives itself)

### Fixed 

- latent out-of-bounds read in the vertical linearization when a component
  had more active variables than assets

- a gap in the `Component_<k>` numbering (e.g. `Component_0` + `Component_2`
  with no `Component_1`) is now rejected instead of silently dropping every
  component past the gap; the count of `Component_*` groups must equal the
  consecutive indices actually read

- the scenario expansion now walks the pool with `next_scenario()` instead of
  sizing the loop with `get_support_size()`, which returns INFScenario (an
  unbounded loop) for a continuous/multi-stage generator; an empty scenario
  pool is rejected rather than producing a degenerate component-less block

- memory leak of a scenario's inner Block when the per-scenario
  InvestmentFunction deserialize threw after the inner had been detached from
  its StochasticBlock shell but before being adopted


## [0.1.1] - 2025-12-12

### Added 

- added Configuration for output Solution

- integration of test/ with common_utils (although test/
  will have to be removed)

- InvestmentBlockSolution and its handling

### Changed 

- adapted to new standard organization of makefiles

### Fixed 

- properly translated investment variable values in
  InvestmentBlockSolution when f\_reformulate\_bounds
  == true


## [0.1.0] - 2024-02-29

- First test release

[Unreleased]: https://gitlab.com/smspp/investmentblock/-/compare/0.1.1...develop
[0.1.1]: https://gitlab.com/smspp/investmentblock/-/compare/0.1.0...0.1.1
[0.1.0]: https://gitlab.com/smspp/investmentblock/-/tags/0.1.0

