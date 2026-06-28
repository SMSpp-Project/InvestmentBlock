# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added 

- `add_component()` to build an InvestmentBlock as a weighted sum of several
  InvestmentFunction components, each exposed separately to BundleSolver (the
  disaggregated path). For now the only way to build it, since the netCDF
  format does not yet describe multiple components; the single-component
  (legacy) path is unchanged.

- `set_weight()` on InvestmentFunction, to weight a component (> 0)

- `set_asset_variable_indices()` on InvestmentFunction, mapping each asset to the
  design variable it invests in, so several components can share the same design
  variables while each one acts on its own subset of them (the multi-period case)

- `set_implicit_constraints()` on InvestmentFunction, to set linking linear
  constraints (such as inter-period monotonicity) on the design variables

- `set_variable_bounds()` on InvestmentBlock, plus accessors for the
  programmatic construction path

### Changed 

- `generate_objective()` now builds a disaggregated sum when there are multiple
  components; the single-component (legacy) path is unchanged

- the `f_reformulate_bounds` option cannot be combined with a multi-component
  construction: it is now rejected with an error rather than silently producing
  wrong results


### Fixed 


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

