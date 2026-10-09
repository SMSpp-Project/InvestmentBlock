# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
