# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- the makefiles build the C++20 module `smspp.investmentblock` when
  `SMSppMOD` is not empty (e.g., `make SMSppMOD=1`), with clang++ 16 or
  later [see `lib/makefile-mod` of SMS++]

- an investment in a `ConversionUnitBlock` is represented by its scale
  factor, as for a `ThermalUnitBlock`, where the unit was refused as
  unrecognized; the unit test checks its linearization

- the C++20 module `smspp.investmentblock` (`src/InvestmentBlock.cppm`),
  which exports every name that the headers declare at namespace scope and
  re-exports `smspp.sddpblock` and `smspp.twostagestochasticblock` and
  `smspp.ucblock`, so that a source file can write `import
  smspp.investmentblock;` in place of the `#include` of the headers; it is
  built with the CMake option `SMSpp_BUILD_MODULE`, as the target
  `SMS++::InvestmentBlock_module`

- components: an InvestmentBlock can be the sum of several
  InvestmentFunction, each in a sub-Block and seen by BundleSolver as a
  component of its own, added with `add_component()` on all the design
  Variable or on a subset of them and returned by
  `get_investment_functions()`; each is weighted by the weight of the
  Objective of its inner Block [see `Block::get_objective_weight()`], with
  a warning when they are all 1. With components the InvestmentBlock is a
  minimization, and a maximization asked at its root is refused; the
  `f_reformulate_bounds` option requires that each component have the lower
  bounds of the design Variable it is active in, and
  `set_number_sub_blocks()` and `set_num_sub_blocks_per_stage()` hold only
  without components

- `AssetVarIndex` and `AssetBaselineVarIndex` (netCDF), with
  `set_asset_variable_indices()` and
  `set_asset_baseline_variable_indices()`: for each asset, the design
  Variable it invests in, so that components can share the design Variable
  or each have its own, and the one its cost is charged against in place
  of `InstalledQuantity`, e.g., the same asset in the previous period

- the netCDF format with components: one `Component_<k>` group for each,
  numbered from 0 with no gaps, holding an InvestmentFunction with its
  `InnerBlock`, whose `ObjectiveWeight` is the weight of the component; the
  indices of design Variable in the file are those of the root, which holds
  no `NumConstraints` or `Constraints_*`, each component having its own
  [see the comment of `InvestmentBlock::deserialize()`]

- `MaxInvestment` and `MaxDisinvestment` (netCDF), with
  `InvestmentFunction::set_variation_limits()`: how much an asset may grow
  over its baseline (`AssetBaselineVarIndex`, or `InstalledQuantity`) and
  how much it may shrink, scalar or one for each asset, >= 0 and + INF when
  absent, which the InvestmentBlock makes linear constraints of its own;
  two components giving the same asset and baseline different limits are
  refused

- an inner Block made of Blocks of the class of the methods of its assets,
  such as a `TwoStageStochasticBlock` or a `MultiStageStochasticBlock` of
  `UCBlock`, is split at load into one component for each such Block [see
  `InvestmentFunction::find_pieces()`], weighted by the weights of the
  Objective from it up to the InvestmentBlock, i.e., the probability of its
  scenario, and `serialize()` writes it back whole; an inner Block, or a
  Block between it and the pieces, with Variable or Constraint of its own
  that tie the pieces is refused when the Constraint are generated, and
  `ScenarioDecomposition` (netCDF) 0 keeps it whole

- `AssetSetter`, `AssetLinearization` and `AssetFeasibilityCut` (netCDF):
  for each asset, the names in the methods factory of the method that
  writes its investment into the inner Block and of the one that reads back
  the derivative of its value, e.g., `UCBlock::resize_unit` and
  `UCBlock::get_resize_unit_linearization`, and whether the latter also
  gives the coefficient of a feasibility cut, 1 by default; the assets
  naming the same methods are written and read with one call each, a file
  without the names has them worked out from `AssetType`, and half a pair
  of names, or a name the methods factory does not have, is refused at
  load (over an `SDDPBlock`, at the first `compute()`)

- `InvestmentFunction::deserialize( group , inner )` and
  `InvestmentFunction::serialize( group , inner )`, which take the inner
  Block from the caller instead of the `InnerBlock` group

- `InvestmentBlock::set_variable_bounds()`,
  `InvestmentBlock::fix_design_variable()` and
  `InvestmentFunction::set_implicit_constraints()`, to build and change an
  InvestmentBlock without a file

- `InvestmentBlockSolution` holds one Solution of the inner Block for each
  component, written as `InnerSolution_<k>` groups with their number in
  `NumInnerSolutions`, or as `InnerSolution` with a single component

- an inner Block in design mode, i.e., one whose units and lines carry the
  design Variable of their own investment, is refused when it is set: each
  Block the investment is written into is asked by the getter
  `<class>::has_design` of the methods factory (`UCBlock::has_design` for a
  `UCBlock`), a class without it having no design mode; the error names
  `design_converter`, the tool of tools/ that converts such files

- `test/README.md`, which describes the folder

### Changed

- the InvestmentFunction treats every inner Block alike: the investment is
  written into its pieces, found also through the Function of the Objective
  of its Blocks when it is a Block (the UCBlock of each stage of an
  SDDPBlock, inside a BendersBFunction), and the value and the linearization
  are read off its Solver; a Solver whose result is the mean of several
  solutions (the simulations of the scenarios of a policy, as the
  `SDDPSimulationSolver` of SDDPBlock) calls the event `eEverySolution`
  after each of them, and the linearization is read out of each, in the
  Block holding it, and averaged. The SDDPBlock wants the
  `SDDPSimulationSolver` as its first Solver [see the BlockSolverConfig of
  the tests and of investmentblock_solver]

- InvestmentBlock depends on the core alone: UCBlock, SDDPBlock and
  TwoStageStochasticBlock are no longer needed to build it (CMake, the
  makefiles, the C++20 module), and whoever reads an inner Block of their
  classes links them

- the pipeline builds the module also with its makefiles, after CMake, and
  runs on a change of the makefiles too [see `ci/makefile` of the umbrella]

- `SMSpp_BUILD_MODULE` no longer stops the configuration when the C++20
  module cannot be built, i.e., with a CMake older than 3.28, a generator
  other than Ninja or Visual Studio, a compiler other than Clang 16 (with
  `clang-scan-deps`) or MSVC 19.34, or a module it imports that is not
  there: the option is turned off with a warning that says why, and the
  build goes on with the headers [see `cmake/CheckModule.cmake` of SMS++]

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

- an asset of type 2 is the second size of any unit that has two, not only
  the converter of a `BatteryUnitBlock`: it pairs with the asset of type 0
  on the same unit, which must be there and name the same methods, or the
  load is refused (over an `SDDPBlock`, at the first `compute()`); a unit
  whose class has no method taking both sizes makes the first `compute()`
  fail

- the key `UnitBlockSolverConfig` of `InvestmentFunction::set_ComputeConfig()`
  is given to every `UCBlock` of the inner Block, as the extra
  Configuration of its `BlockConfig`, since the `UCBlock` computes the
  derivative by copies [see `UCBlock::get_replicate_linearization()`];
  without it the "BlockSolverConfig" of the inner Block takes its place,
  and over an `SDDPBlock` it is needed when an asset can have no copies

- `InvestmentFunction` no longer looks at the class of its inner Block, an
  `SDDPBlock` apart: it takes any inner Block the methods of its assets can
  be called on, such as a `MultiStageStochasticBlock`, and refuses the
  others at load, where it refused all but an `SDDPBlock`, a
  `TwoStageStochasticBlock` and a `UCBlock`; a name "<class>::<method>" is
  called only on a Block of that class

- a netCDF variable with one value for each asset, or for each linear
  constraint, of InvestmentBlock and InvestmentFunction is refused at load
  when its size is neither 1 nor that of `NumAssets` (`NumConstraints`)

- `InvestmentFunction::compute()` returns `kUnbounded` when the Solver of
  the inner Block proves it unbounded, at the opposite of the worst value
  and with no linearization, as `LagBFunction` and `BendersBFunction` do:
  it returned `kError`, or -Inf with a linearization read from whatever
  duals the Solver had

- `InvestmentFunction::get_var_lower_bound()` is public

### Removed

- what the InvestmentFunction did by itself with an SDDPBlock (the training
  by its SDDPSolver and the simulation of each scenario on a copy by its
  SDDPGreedySolver, with OpenMP and MPI), now the `SDDPSimulationSolver` of
  SDDPBlock, and with it the inner Block made of several identical
  SDDPBlock (`InvestmentFunction::set_inner_blocks()`,
  `set_number_sub_blocks()` and `set_num_sub_blocks_per_stage()` of both
  classes), which nothing set, and the parameters `intOutputSolution` and
  `strOutputSolutionDirectory`, which only that path read

- an SDDPBlock is no longer kept whole by its class when the inner Block is
  split: what is behind a Function is not split, whatever the class

- the attributes `ReplicateBatteryUnits` and `ReplicateIntermittentUnits`
  of InvestmentFunction, which `AssetSetter` replaces: a group carrying
  either is refused at load

- the casts of InvestmentFunction to `ThermalUnitBlock`,
  `BatteryUnitBlock`, `IntermittentUnitBlock` and `DCNetworkBlock`, and the
  derivative by copies of a unit that it computed out of the rows of the
  UCBlock: the methods each asset names do both [see `AssetSetter`]

- `InvestmentBlock::get_integer()`, and the overloads of
  `InvestmentBlock::set_variable_values()` taking an `Eigen::ArrayXd` or an
  iterator, which nothing called: `set_variable_values()` takes a
  `std::vector`

### Fixed

- `InvestmentFunction::set_par()` that shrinks the global pool
  (`intGPMaxSz`) tells removed the linearizations past its new size: the
  `GlobalPoolRemoved` Modification had an empty set of names, which says
  that all of them were removed

- a `State` of a smaller global pool put into an `InvestmentFunction`
  leaves no linearization in the names past it: the copy read the given
  pool past its end, and kept the constants of the names past it, so that
  they still looked stored

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

- `InvestmentFunction::compute()` returns `kError` when the inner Block is
  solved but its linearization cannot be read, e.g., because its Solver
  gives no dual solution: it returned the status of the solve, with no
  value and no linearization, and a bundle above it could declare an
  optimum of 0

- a throw in `InvestmentFunction::compute()`, its own or of the Solver of
  the inner Block, leaves the inner Block unlocked and gives the Solver its
  identity back

- `remove_variable()` and `remove_variables()` of InvestmentFunction remove
  all the data of the assets sized by the removed Variable: the costs, the
  installed quantity and the columns of the linear constraints stayed at
  their position

- the dimension `ObjectiveSense` of InvestmentBlock is read as documented,
  and as BendersBlock reads it: size 0 is a maximization, any other size a
  minimization; its size was taken as the sense itself, so that size 1 was
  a maximization

- a deserialize that is refused frees what it had built: the
  InvestmentFunction of the single-component format, and an inner Block
  that InvestmentFunction refuses, were leaked

- `InvestmentBlock::serialize()` with a single component no longer fails:
  InvestmentFunction wrote `NumAssets` and `LowerBound` again in the group
  of the InvestmentBlock, which already holds them, and left the group
  with its own type

- `InvestmentFunction::get_dflt_str_par()` has a default for each of its
  two string parameters: the default of `strOutputSolutionDirectory` was
  read past the end of a vector, which made every reset to the defaults
  crash, as `investmentblock_solver` does after solving

- over several replicas of an `SDDPBlock`, `InvestmentFunction` sums the
  linearization into a vector as long as the active Variable: it was as
  long as the last one computed, empty before the first, and the first
  coefficient was written out of it

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

### Changed

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

### Fixed

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
