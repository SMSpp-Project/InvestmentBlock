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

- the data archive 2026-09-26, which adds to `pypsa-data` one scenario of
  the modular family of pypsa2smspp with the design in an InvestmentBlock
  over the UCBlock (`smspp_mod_t48_s1_b2c_det_investment.nc`), the same
  network the archive of UCBlock has with the design in the units

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

- the data archive is downloaded by version: `DATA_VERSION` in CMakeLists.txt
  names the version of the Package Registry to read, and the archive and the
  marker of its extraction carry it in their name, so that a tree holding
  an older extraction (the cache of the CI, or a clone extracted before)
  downloads and extracts again instead of running on the old data;
  data/upload-nc4 publishes the archive under that version

- `InvestmentFunction::compute()` says which Solver of the inner Block
  returned which status when that Solver gives no solution and no proof that
  there is none: the caller reads `kError` and nothing else, so a run that
  stopped there told neither what answered nor what it answered

- the comment of the feasibility cut says why a certificate of infeasibility
  supports no cut against a scaled design, a kappa entering the constraints
  it appears in through their right-hand side alone, with the numbers of the
  instance where it was measured

- the two indices of a unit under investment are named inside the loop and
  not by a structured binding of it, a structured binding being what a lambda
  cannot capture when OpenMP is on

- the walk over what a Block holds asks the Block for its groups of
  Constraint, one run at a time, rather than the vectors of `boost::any` that
  are not there any more: the constant of a cut is summed over them, and a
  combination of linearizations keeps the coefficients of its constituents
  whatever their number

- whoever links the module keeps it: the classes of a module register
  themselves in the factory from a static initialiser, and a linker that
  drops what looks unused takes the registration away with it, so the target
  now tells whoever links it to keep the symbol that forces the module in,
  and on ELF, where naming the symbol is not enough, the library as a whole

### Removed

- the optional `NumComponents` dimension (the component count is implicit in
  the `Component_<k>` suffixes)

- `get_variables_writable()` (superseded by `fix_design_variable()` and by
  `add_component()` wiring the actives itself)

### Fixed

- on macOS a program linking the module lost the classes the module
  registers in the factories when the linker dropped the library, as it
  does under `-dead_strip_dylibs`, which conda sets: the target now asks the
  linker for the symbol that forces the module in (`-u`), which ld64,
  unlike the ELF linker, counts as a use of the library

- with more than one MPI process, every process of an InvestmentFunction over
  an SDDPBlock gets the value and the linearization that process 0 simulates:
  the others used to come back with neither, so that their Solver stopped at
  the first point and left process 0 waiting in the next training

- the derivative with respect to the scale factor of a unit takes in the
  rows of the pollutant budget, the storage levels of the unit included

- after a compute that fails, the Function hears again the Modification of
  its inner Block

- the derivative with respect to the scale factor of a unit subtracts its
  fixed consumption when it is off

- the step that fetches the data archive of this module says what went wrong
  when it goes wrong: the download is checked, an archive that did not arrive
  is removed instead of being left on disk for the build to take for the real
  one, and the message names the URL. A server that answers with an error page
  used to leave a file of a few bytes there, which made the next build fail
  while extracting it, with the message of `tar` and no mention of the
  download

- makefile-c takes TwoStageStochasticBlock from its makefile-s, so that the
  core SMS++ objects are not listed twice

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

## [0.2.0] - 2026-09-12

### Added

- the inner Block of an InvestmentFunction can be a TwoStageStochasticBlock,
  whose here-and-now Variable then live outside the scenarios in a single
  copy: the investment is written into every leaf of the scenario structure,
  and the linearization is read from every sub-Block that carries it

- the value function has a vertical linearization where the inner Block is
  infeasible, i.e., the feasibility cut of a Benders decomposition, and a
  provably infeasible inner subproblem is reported as kInfeasible rather than
  kError

### Changed

- the linearization in the capacity of a unit is asked to
  `UnitBlock::get_kappa_linearization()`

- the accessor to the inner Block is gone, `get_nested_Blocks()` being it

- the version of the module is the git tag of its repository, or the
  VERSION.txt of a release tarball, and the shared library carries it: its
  SONAME is major.minor while the major is 0, and it is installed with an
  RPATH relative to itself, so that an installed tree keeps working wherever
  it is moved

### Fixed

- the derivative of a flow limit carries the factor the limit is scaled by

- each replica of the inner Block of an InvestmentFunction is un-configured
  with the clone that configured it

- the package configuration file finds the libraries the module links, so that
  a project using the installed module needs nothing more than find_package(),
  StOpt comprised

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

### Added

- First test release

[Unreleased]: https://gitlab.com/smspp/investmentblock/-/compare/0.2.0...develop
[0.2.0]: https://gitlab.com/smspp/investmentblock/-/compare/0.1.1...0.2.0
[0.1.1]: https://gitlab.com/smspp/investmentblock/-/compare/0.1.0...0.1.1
[0.1.0]: https://gitlab.com/smspp/investmentblock/-/tags/0.1.0

