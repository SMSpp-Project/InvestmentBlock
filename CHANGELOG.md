# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

### Changed

### Fixed

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

- First test release

[Unreleased]: https://gitlab.com/smspp/investmentblock/-/compare/0.2.0...develop
[0.2.0]: https://gitlab.com/smspp/investmentblock/-/compare/0.1.1...0.2.0
[0.1.1]: https://gitlab.com/smspp/investmentblock/-/compare/0.1.0...0.1.1
[0.1.0]: https://gitlab.com/smspp/investmentblock/-/tags/0.1.0

