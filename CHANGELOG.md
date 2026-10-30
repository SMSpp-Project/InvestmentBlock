# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- the makefiles build the C++20 module `smspp.investmentblock` when
  `SMSppMOD` is not empty (e.g., `make SMSppMOD=1`), with clang++ 16 or
  later [see `lib/makefile-mod` of SMS++]

- the limits on the change of an asset from one period to the next: the
  optional fields `MaxInvestment` and `MaxDisinvestment` of a
  `Component_<k>`, scalar or one for each asset, each >= 0 and + INF when
  absent, bound x - b, b being the baseline of the costs
  (`AssetBaselineVarIndex`, or `InstalledQuantity`), between
  -`MaxDisinvestment` and `MaxInvestment`, and are read, written and set with
  `InvestmentFunction::set_variation_limits()`. The InvestmentBlock makes
  them `FRowConstraint` of its own, one for each pair of design Variable and
  baseline, which BundleSolver passes to its master problem; two components
  giving the same pair different limits are refused

- `InvestmentBlock::deserialize()` refuses an inner Block in design mode,
  i.e., one whose units and lines carry the design Variable of their
  investment (an `IntermittentUnitBlock`, a `ThermalUnitBlock`, a
  `NuclearUnitBlock` or a `ConversionUnitBlock` with an investment cost, a
  `BatteryUnitBlock` with a battery or converter investment cost, a
  `DesignNetworkBlock`); the error names `design_converter`, the converter of
  such files

- the type of asset 2, the second size of a unit that has two, for a
  `BatteryUnitBlock` its converter: an asset of type 0 and one of type 2 on
  the same unit form a pair, written and read back together through the
  forms with two spans of their names, the first value for the asset of
  type 0. A type 2 with no type 0 on its unit is refused at load; a pair
  whose two assets name different methods is refused at load too
  (over an `SDDPBlock`, at the first `compute()`), and a pair on a unit
  whose class has no form with two spans makes the first `compute()` fail

- the key "UnitBlockSolverConfig" of `InvestmentFunction::set_ComputeConfig()`,
  the `BlockSolverConfig` of the copy of a unit with no copies, which the
  derivative by copies of a `UCBlock` minimises (`UCBlock` documents it);
  `InvestmentFunction` gives a clone of it to every `UCBlock` of the inner
  Block, as the extra Configuration of its `BlockConfig`, and takes it back
  when the `BlockSolverConfig` of the inner Block is reset. Without it the
  "BlockSolverConfig" given with it, if any, is given in its place, so that
  the copy is solved by the Solver of the inner Block, given explicitly as
  `UCBlock` wants it; an `SDDPBlock`, which has no pieces, needs the key
  when an asset can have no copies

- an investment in a `ConversionUnitBlock` is represented by its scale
  factor, as for a `ThermalUnitBlock`, where the unit was refused as
  unrecognized; the unit test checks its linearization

- `ScenarioDecomposition` (netCDF), an optional dimension of the root: 0
  keeps the inner Block whole, 1, the default, splits it into components
  [see below], and serialize writes the dimension only when it is 0

- `add_component()` to build an InvestmentBlock as a weighted sum of several
  InvestmentFunction components, each exposed separately to BundleSolver (the
  disaggregated path), each weighted by the weight of the Objective of its
  inner Block [see `Block::get_objective_weight()`], which the
  InvestmentFunction applies to its value and its diagonal linearization;
  an InvestmentBlock with components is a minimization
  [see `get_objective_sense()`], so that a Solver reports the value of the
  solution it writes, and `set_number_sub_blocks()` and
  `set_num_sub_blocks_per_stage()` hold only without components.

- netCDF (1:K) format for the disaggregated InvestmentBlock: the group may now
  hold one `Component_<k>` sub-group per component (the presence of
  `Component_0` selects the disaggregated path; the component count is
  implicit in the suffixes), each a legacy InvestmentFunction description
  weighted by the `ObjectiveWeight` of its `InnerBlock` and, for the
  multi-period case, its own
  `AssetVarIndex` and `AssetBaselineVarIndex` variables; all variable indices
  in the file are GLOBAL (positions in the design array of the root); both
  deserialize and serialize support it, the comment of
  `InvestmentBlock::deserialize()` describes it, and `NumConstraints`,
  `Constraints_*` and a maximization at the root are refused, each component
  holding its own linear constraints and being a minimization

- `AssetBaselineVarIndex` (netCDF) and
  `set_asset_baseline_variable_indices()`: per-asset *variable* baseline for
  the multi-period case — the transition cost of asset i is charged against
  the value of another design variable (typically the same asset's variable
  in the previous period) instead of the `InstalledQuantity` datum, with the
  mirrored subgradient entry on the baseline variable; cannot be combined
  with `InstalledQuantity`

- `add_component( f , actives )` overload binding a component to a
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

- `InvestmentFunction::deserialize( group , inner )`, which takes the inner
  Block from outside instead of creating it out of the `InnerBlock` group
  (this is how the split below hands each piece to its component)

- an inner Block, the `InnerBlock` of a `Component_<k>` or the direct one,
  made of Blocks of the class of the methods of its assets other than
  itself, found going down from it only through Blocks whose sub-Blocks are
  all of that class or made so in turn [see
  `InvestmentFunction::find_pieces()`], such as a TwoStageStochasticBlock or
  a MultiStageStochasticBlock of UCBlock, is split by deserialize into one
  component per such Block, with no cast to its class: each piece is
  weighted by the weights of the Objective from it up to the
  InvestmentBlock, the probability of a scenario or of a leaf. Nothing is
  generated at load: when the InvestmentBlock generates its Constraint, once
  the Solver of every piece is registered, a split inner Block, or a Block
  between it and the pieces, with Variable or Constraint of its own, which
  tie the pieces, is refused, asking for a `ScenarioDecomposition` of 0.
  The inner Block is kept until the InvestmentBlock is destroyed, and
  serialize() writes it whole in place of its components, as one
  `Component_<k>`; when the one at the root is split, a maximization asked
  at the root is rejected

- `get_investment_functions()` on InvestmentBlock, which returns its
  InvestmentFunction, or that of each of its components

- `InvestmentFunction::serialize( group , inner )`, which writes the
  InvestmentFunction with the given inner Block

- `InvestmentBlockSolution` now carries one inner Solution per component,
  serialized as `InnerSolution_<k>` groups with a `NumInnerSolutions`
  round-trip guardrail; the legacy single-`InnerSolution` format is unchanged

- `set_asset_variable_indices()` on InvestmentFunction, mapping each asset to the
  design variable it invests in, so several components can share the same design
  variables while each one acts on its own subset of them (the multi-period case)

- `AssetSetter` and `AssetLinearization` (netCDF): for each asset, the names
  in the methods factory of the method that writes the investment into the
  inner Block and of the one that reads back the derivative of its value,
  both called on the UCBlock at the root of the inner Block (or on each of
  them, the leaves of a TwoStageStochasticBlock and the stages of an
  SDDPBlock) with the index of the asset, for instance
  `UCBlock::resize_unit`, `UCBlock::replicate` and `UCBlock::resize_line`
  with their getters. InvestmentFunction does not know what they do: the
  assets naming the same methods are written with one call, and read back
  with one call. `AssetFeasibilityCut` says whether the getter also gives
  the coefficient of a feasibility cut out of an unbounded dual direction, 1
  by default.
  A file without the names has them worked out from `AssetType` and from
  whether the class of each unit registers `<class>::resize`; half of the
  pair of names is refused at deserialize, and a name the methods factory does not have too (over an `SDDPBlock`,
  at the first `compute()`, before the solve)

- `set_implicit_constraints()` on InvestmentFunction, to set the implicit
  linear constraints (caps or budgets over the assets of the component)
  programmatically; one coefficient per ASSET, as the netCDF format declares

- `set_variable_bounds()` on InvestmentBlock, plus accessors for the
  programmatic construction path

- the C++20 module `smspp.investmentblock` (`src/InvestmentBlock.cppm`),
  which exports every name that the headers declare at namespace scope and
  re-exports `smspp.sddpblock` and `smspp.twostagestochasticblock` and
  `smspp.ucblock`, so that a source file can write `import
  smspp.investmentblock;` in place of the `#include` of the headers; it is
  built with the CMake option `SMSpp_BUILD_MODULE`, as the target
  `SMS++::InvestmentBlock_module`

### Changed

- `test/BSPar_osimp.txt` and the notes on the master problem in `README.md`
  speak the configuration dialect of BundleSolver 2.0, and the comments of
  `test/BSPar_osimp.txt` are within 79 columns; `test/README.md` and
  `test/.gitignore` describe the folder without the old local suite, which
  `CMakeLists.txt` adds as the hook of the local one

- the pipeline builds the module also with its makefiles, after CMake, and
  runs on a change of the makefiles too [see `ci/makefile` of the umbrella]

- `SMSpp_BUILD_MODULE` no longer stops the configuration when the C++20
  module cannot be built, i.e., with a CMake older than 3.28, a generator
  other than Ninja or Visual Studio, a compiler other than Clang 16 (with
  `clang-scan-deps`) or MSVC 19.34, or a module it imports that is not
  there: the option is turned off with a warning that says why, and the
  build goes on with the headers [see `cmake/CheckModule.cmake` of SMS++]

- `InvestmentFunction` no longer looks at the class of its inner Block while
  it computes, but for an `SDDPBlock`: when the inner Block is set it keeps
  an `AbstractPath` to each Block the investment is written into and read
  back from (each leaf of a `TwoStageStochasticBlock` or
  `MultiStageStochasticBlock`, else the inner Block itself), the Solver is
  the first one registered with the inner Block, and the names the instance
  does not give are worked out of the classes of the units then, not at the
  first `compute()`, and are not written by `serialize()`. A name
  "<class>::<method>" is called only on a Block of that class, and otherwise
  deserialize fails (the first `compute()`, after a new inner Block), where
  a Block of another class was cast to `UCBlock` and refused

- `InvestmentFunction::deserialize` no longer refuses an inner Block by its
  class (an `SDDPBlock`, a `TwoStageStochasticBlock` or a `UCBlock`, and
  only the first or the last when given from outside): once the inner Block
  is set, it resolves the methods of the assets, and refuses an inner Block
  they cannot be called on, deleting it, or handing it back when given from
  outside; over an `SDDPBlock` this still waits for the first `compute()`

- the instances of `data/` are downloaded and extracted by the targets
  `download_investment_<fmt>` and `extract_investment_<fmt>`, written as
  in every module that keeps its instances in the Package Registry, and
  the marker of the extraction carries the format in its name, so that a
  tree extracted before extracts once more

- the linearization of the scale of a unit takes its terms in the balances
  of all the carriers of the UCBlock, each with the efficiency of its
  outputs, from `UCBlock::for_each_balance_term()`, and its pollutant zones
  from `UCBlock::get_generator_pollutant_zone()`, instead of the node of
  each generator on the electricity, whose map is gone

- the line of an asset of type 1 may be of any carrier of the UCBlock, the
  lines of the carriers being numbered one carrier after the other, those
  of the electricity first (the line l of carrier c is L_0 + ... + L_{c-1} +
  l), and its investment is the kappa of that line in the NetworkBlock of
  its carrier, which must be one per time instant

### Fixed

- `InvestmentFunction::deserialize( group , inner )` hands the inner Block
  back also when `Block::deserialize()` refuses the group (a weight of the
  Objective that is not positive), as when the methods of the assets
  cannot be called on it: the function deleted it, and the caller used it
  after, when splitting an inner Block

- `makefile-c` and `makefile-s` include the basic makefile of
  TwoStageStochasticBlock, whose StochasticBlock comes with SDDPBlock
  already: with both makefile-s it was compiled twice, from two paths, and
  whatever linked InvestmentBlock failed on its multiple definitions

- an `InvestmentFunction` over a `TwoStageStochasticBlock` or a
  `MultiStageStochasticBlock` asks the Solver for the solution once per
  evaluation, and no longer once per scenario: every request wrote the
  solution of the whole inner Block again, so that an evaluation cost the
  square of the number of scenarios; the values and the linearizations do
  not change

- a `MultiStageStochasticBlock` whose first-stage paths are only in the
  `TwoStageStochasticBlock` of a lower level, below an inner
  `MultiStageStochasticBlock`, is kept whole: the deserialize looked for
  them at the root and its sub-Blocks only, hence it separated the
  scenarios, dropping the ties among those below, and solved a relaxation
  with no warning
- an exception in the simulation of a scenario of an `InvestmentFunction`
  over an `SDDPBlock` (the copy of the SDDPBlock that cannot be built, or a
  `compute()` that throws) is an error of the scenario, as a `compute()`
  that finds nothing, rather than an exception leaving the parallel loop,
  which ended the program
- the simulation of an `InvestmentFunction` over an `SDDPBlock` runs each
  scenario on a copy of the SDDPBlock, as `sddp_solver` does, so that the
  state a stage passes to the next reaches units whose Variable are not yet
  generated: a `ThermalUnitBlock` that switched in a stage refused the
  `InitUpDownTime` of the next, which kept that of the data (and missed,
  e.g., the cost of its minimum up time); the data of the inner Block are
  no longer changed by the simulation

- the linearization of the scale of a unit whose outputs are not the
  electricity alone, or whose generators have no node on the electricity,
  which gave the active power the dual of the electric row only, with
  coefficient 1, and the pollutant zone of the node 0

## [0.3.0] - 2026-10-09

### Added

- the asset type 2 (`AssetType`), the converter of a `BatteryUnitBlock`,
  whose investment is the kappa of the converter
  (`BatteryUnitBlock::set_converter_kappa()`) with its own cost, bounds and
  linearization: with an asset of type 0 on the same battery the storage
  and the converter are sized apart, while a battery with no asset of type
  2 is resized as a whole by its kappa, as before

- the key `UnitBlockSolverConfig` of the extra Configuration of
  `InvestmentFunction`, the BlockSolverConfig of the copy of a scaled unit
  with no copies that the linearization solves; if absent, with a UCBlock
  as inner Block its BlockSolverConfig is used

- `test/`, the unit test of the module (`InvestmentBlock_unit_test`): the
  curvature `InvestmentFunction` declares, and, with a :MILPSolver in the
  build, its linearization against the finite differences of its value for
  a line of a meshed network and for a scaled unit giving reactive power

- the netCDF variable `Integer`, scalar or per asset, which makes the
  ColVariable of the assets where it is nonzero integer, e.g., the number of
  modules of a modular asset; a `BundleSolver` with `intIntVars` 1 keeps
  them integer

- the data archive 2026-09-26, which adds to `pypsa-data` one scenario of
  the modular family of pypsa2smspp with the design in an InvestmentBlock
  over the UCBlock (`smspp_mod_t48_s1_b2c_det_investment.nc`), the same
  network the archive of UCBlock has with the design in the units

- the `test/` folder, which holds `test/BSPar_osimp.txt`, the parameters of
  BundleSolver 2.0 that the test suite of the module reads, `test/README.md`,
  which says that the developers keep that suite locally and not in this
  repository, and `test/.gitignore`; the build enables CTest and adds the
  folder only where a `test/CMakeLists.txt` is present

### Changed

- a variable with one value for each asset, or for each linear constraint,
  of InvestmentBlock and InvestmentFunction is read as a scalar, or of size
  1, holding for all, or of the size of `NumAssets` (`NumConstraints`), and
  any other size is refused at deserialize: a longer one was cut without a
  word, and one of size 1 was refused by netCDF without saying which

- an inner Block that its Solver proves unbounded makes
  `InvestmentFunction::compute()` return `kUnbounded` at the opposite of the
  worst value, with no linearization, as LagBFunction and BendersBFunction
  return the status of their Solver: it ended in `kError`, or in -Inf with a
  linearization read from whatever duals the Solver had

- `generate_objective()` now builds a disaggregated sum when there are multiple
  components; the single-component (legacy) path is unchanged

- the `f_reformulate_bounds` option works with several components when each
  of them has the lower bounds of the design variables it is active in, and
  neither `AssetVarIndex` nor `AssetBaselineVarIndex`; otherwise it is
  rejected with an error rather than silently producing wrong results

- `InvestmentFunction::get_var_lower_bound()` is public

- the deserialize consistency check between the number of assets and of
  active variables is now mapping-aware (a component with an `AssetVarIndex`
  may invest in a subset of the design variables)

- the implicit constraints now follow the PER-ASSET semantics the netCDF
  format declares (`Constraints_A` is NumConstraints × NumAssets): column j
  applies to the variable of asset j through the mapping — in constraint
  evaluation and in the vertical linearizations alike; under the legacy
  identity mapping the behaviour is bit-identical

- `add_component()` wires the active variables itself when the component has
  none (all the design variables, in natural order); `is_disaggregated()` is
  now structural (`get_number_nested_Blocks() > 0`)

- `InvestmentFunction` throws when the asset is a line that has a design
  variable in the `DCNetworkBlock`, whose kappa multiplies that variable,
  instead of giving it a zero linearization; the documentation states the
  convexity of a battery in its kappa through the continuous relaxation

- the class comments of InvestmentFunction.h write the function (cost of
  the investment plus operational cost), how each asset enters the inner
  Block, the linearization and when it is a subgradient, its convexity and
  domain, and what a capacity expansion model may have that it has not (a
  worst case over models of the uncertainty, for which they give the
  formulation with one InvestmentFunction per model); the doc says the
  defaults the code has, `InstalledQuantity` 0 and no bounds on the
  ColVariable of the InvestmentBlock, and all its formulae are in LaTeX

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

- the `ReplicateBatteryUnits` and `ReplicateIntermittentUnits` attributes of
  InvestmentFunction, which sized by replication every battery or every
  intermittent unit: how each asset is sized is said by `AssetSetter`, and
  a group that still carries either attribute is now refused at deserialize

- what InvestmentFunction knew of the classes of the assets: the casts to
  `ThermalUnitBlock`, `BatteryUnitBlock`, `IntermittentUnitBlock` and
  `DCNetworkBlock` that chose what to call on each asset, and the copy of the
  derivative with respect to the number of copies of a unit that it computed
  out of the rows of the UCBlock, with the map from generators to nodes that
  served it; the UCBlock answers for both [see `AssetSetter`]

### Fixed

- a throw in `InvestmentFunction::compute()`, its own or of the Solver of the
  inner Block, leaves the inner Block unlocked and gives the Solver its
  identity back: both were left to the function, as when the Solver returns
  the multipliers of an unbounded dual direction with the Objective in them
  (`intHomogeneousDirection` 0)

- `remove_variable()` and `remove_variables()` of InvestmentFunction remove
  the assets sized by the removed Variable with all of their data: the
  single removal left the costs, and all of them the installed quantity and
  the columns of the linear constraints, at the position of the removed
  asset; with a mapping [see `set_asset_variable_indices()`] the Variable of
  the assets that stay, and of their baselines, are moved to their new
  positions, and a removal that would leave an asset without its baseline
  is refused

- the comment of `InstalledQuantity` says its default, 0, which
  `get_installed_quantity()` gives: it said 1

- `InvestmentFunction::is_convex()` returns false with no inner Block or with
  an inner Block that maximizes, as its comment says: it returned true, so
  that a bundle minimized as convex a function that is neither convex nor
  concave; `is_concave()` keeps returning false, and its comment says why

- the dimension `ObjectiveSense` of InvestmentBlock is read as documented,
  and as BendersBlock reads it: size 0 is a maximization, any other size a
  minimization; its size was taken as the sense itself, so that size 1 was
  read as a maximization

- a rejected deserialize frees what it had built: `add_component()` deletes
  the component on every error, as it promises, the single-component format
  deletes its InvestmentFunction, `InvestmentFunction::deserialize()` deletes
  an inner Block of a class it does not accept, and a TwoStageStochasticBlock
  with a rejected scenario gets back the scenarios already handed out and is
  deleted with them

- the serialize of the single-component format no longer fails:
  InvestmentFunction added the dimension `NumAssets` and the variable
  `LowerBound` again to the group of the InvestmentBlock, which already holds
  them, and left the group with its own type

- `InvestmentFunction::get_dflt_str_par()` has a default for each of its two
  string parameters: it had one, and the default of
  `strOutputSolutionDirectory` was read past the end of the vector, which made
  every reset to the defaults crash, as `investmentblock_solver` does after
  solving

- the path of `InvestmentFunction` over several replicas of an SDDPBlock
  summed the linearization into a vector as long as the last one computed,
  which is empty before the first, and wrote the first coefficient out of
  it: it is now as long as the active Variables

- an InvestmentFunction that finds no UCBlock in its inner Block reports it
  with `kError` at the worst value: asking the sign of that value looked for
  the UCBlock again, and failed in turn, with an assertion or a null
  pointer; the sense of the Objective comes now from the inner Block when
  there is no UCBlock

- `InvestmentFunction::compute()` returns `kError` when the inner Block is
  solved but its linearization cannot be read, e.g. because its Solver gives
  no dual solution, and a call with nothing changed returns it again: it
  used to return the status of the solve, with no value and no
  linearization, and a bundle above it went on and could declare an optimum
  of 0

- the linearization of a unit represented by its scale factor with no
  copies: the operational part of the coefficient was read from the
  solution of the unit, in which it weighs nothing, and was 0 (the unit
  being off) also where building it pays, so that a method starting at 0
  copies stopped there; it is the value at the dual values of a copy of the
  unit solved alone, relaxed as in the inner Block, by the Solver of
  `UnitBlockSolverConfig`, which is a subgradient

- the linearization with respect to a line no longer asserts that the
  network is HVDC, which aborted builds with assertions on any network with
  susceptances, the derivative of the flow limits being the same there

- the linearization with respect to a unit represented by its scale counts
  the reactive node injection constraints, which also carry the scaled
  reactive power of the unit and were left out

- `is_convex()` and `is_concave()` look at the inner Block, which has to
  be there and minimize (maximize), and at the cost of investing plus that
  of disinvesting in each asset, `c + d`, which has to be nonnegative
  (nonpositive) for the investment cost to be convex (concave); they
  returned `true` and `false` whatever the InvestmentFunction

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

[Unreleased]: https://gitlab.com/smspp/investmentblock/-/compare/0.3.0...develop
[0.3.0]: https://gitlab.com/smspp/investmentblock/-/compare/0.2.0...0.3.0
[0.2.0]: https://gitlab.com/smspp/investmentblock/-/compare/0.1.1...0.2.0
[0.1.1]: https://gitlab.com/smspp/investmentblock/-/compare/0.1.0...0.1.1
[0.1.0]: https://gitlab.com/smspp/investmentblock/-/tags/0.1.0
