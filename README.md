# InvestmentBlock

[![Pipeline](https://gitlab.com/smspp/investmentblock/badges/develop/pipeline.svg)](https://gitlab.com/smspp/investmentblock/-/pipelines?ref=develop)
[![Release](https://gitlab.com/smspp/investmentblock/-/badges/release.svg)](https://gitlab.com/smspp/investmentblock/-/releases)
[![Documentation](https://img.shields.io/badge/docs-Doxygen-blue)](https://smspp.gitlab.io/smspp-project/)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![License: LGPL v3](https://img.shields.io/badge/License-LGPL_v3-blue.svg)](https://www.gnu.org/licenses/lgpl-3.0)

SMS++ Block for capacity expansion: the investment in the assets of an
inner Block, such as the units and the lines of a `UCBlock`.

The `InvestmentBlock` class, which derives from `Block`, has a vector of
`ColVariable`, each of which represents the investment in a particular
asset, with its bounds and, optionally, its integrality. Its `Objective` is
the sum of one or more `InvestmentFunction`: a single one is the `Function`
of its `FRealObjective`, while several are its components, each in a
sub-`Block` of its own and weighted by the weight of the `Objective` of its
inner `Block` (see `Block::get_objective_weight()`), which `BundleSolver`
sees as separate components. The components may share the same
`ColVariable`, as the scenarios of a stochastic problem do, or each invest
in its own and charge its cost against those of another, as the periods of
a multi-period problem do (see `AssetVarIndex` and `AssetBaselineVarIndex`
in `InvestmentFunction`). The components are solved independently of each
other: an inner `Block` whose state passes from one period to the next is
not solved as such when its periods are written as separate components.

Besides the bounds, the `InvestmentBlock` has a linear constraint for each
limit on how much an asset may grow or shrink from its baseline (see
`MaxInvestment` and `MaxDisinvestment` in `InvestmentFunction`), and each
`InvestmentFunction` may have "implicit" linear constraints on its assets,
which it generates itself as "vertical linearizations".

An `InvestmentFunction` is a `C05Function` that is capable of evaluating the
investment on a set of assets. `InvestmentFunction` derives from *both*
`C05Function` and `Block`. The main ingredients of an `InvestmentFunction` are
the following:

- The active `ColVariable` of the `InvestmentFunction` represent investments
  that can be made in certain assets. What investing in an asset means is
  said by the asset itself, which names in the methods factory the method
  that writes the investment into the inner `Block` and the one that reads
  back the derivative of the value with respect to it (see `AssetSetter` and
  `AssetLinearization` in `InvestmentFunction`). Those of `UCBlock` invest
  an amount x in

  * a unit that stands for x identical copies of itself (see
    `UCBlock::replicate()` and `UnitBlock::scale()`), such as a
    `ThermalUnitBlock` or a `ConversionUnitBlock`;

  * a unit resized to x in the way of its class (see
    `UCBlock::resize_unit()`), such as an `IntermittentUnitBlock` or a
    `BatteryUnitBlock` whose minimum and maximum power and storage levels
    are scaled by x using `set_kappa()`; the storage and the converter of a
    `BatteryUnitBlock` can also be sized apart, with an asset each (see
    `AssetType` in `InvestmentFunction`);

  * a line, of any carrier, whose minimum and maximum power flow are scaled
    by x using `set_kappa()` (see `UCBlock::resize_line()`).

- The value of the `InvestmentFunction` is given by a fixed investment cost
  (CAPEX) and an [expected] operational cost (OPEX). The fixed investment cost
  is a linear function of the `ColVariable` of the `InvestmentFunction`. The
  [expected] operational cost is given by the solution value of the inner
  `Block` (which depends on the investment being made, i.e., the values of
  the variables of the `InvestmentFunction`).

- The `InvestmentFunction` has an inner `Block` which contains the assets to
  invest in: any `Block` the methods of its assets can be called on, such as
  a `UCBlock`, a `TwoStageStochasticBlock` or a `MultiStageStochasticBlock`
  of `UCBlock`, or an `SDDPBlock`. An inner `Block` made of `Block` of the
  class of those methods, as the scenarios of a `TwoStageStochasticBlock`,
  is split when the `InvestmentBlock` is read into one component for each of
  them, weighted by its probability; if the inner `Block` ties them with
  `Variable` or `Constraint` of its own, `ScenarioDecomposition` 0 keeps it
  whole. Over an `SDDPBlock` the OPEX is the expected cost of its
  sub-`Block`, some of which (the ones where the investments take place)
  must themselves be `UCBlock`, and the same amount of investment in the
  assets is made for all its stages.

An inner Block in *design mode*, i.e., one whose units and lines carry the
design Variable of their investment, as a `TwoStageStochasticBlock` written
to be solved as a whole, is refused when the `InvestmentBlock` is read (see
`InvestmentBlock::deserialize()`): the investment is decided once, by the
`InvestmentBlock`, so such a file has to be converted first into one of
`InvestmentBlock`, each design an asset.

The netCDF format of an `InvestmentBlock` is described in the comments of
`InvestmentBlock::deserialize()` and `InvestmentFunction::deserialize()`.


## Getting started

These instructions will let you build `InvestmentBlock` on your system.

The module also comes ready-made, in any of

```sh
sudo add-apt-repository ppa:smspp-project/smspp   # Ubuntu
sudo apt install libsmspp-investment-dev          # and smspp-investment for the tool

conda install -c conda-forge smspp-project        # Linux, macOS, Windows

brew tap SMSpp-Project/smspp                      # macOS, Linux
brew install smspp

vcpkg install "smspp[core,investment]"            # from the sources
```

where apt and the port give the module alone, while conda and the tap carry
the whole framework. What follows is about building it yourself.

### Requirements

- The [SMS++ core library](https://gitlab.com/smspp/smspp) and its
  requirements.

- [SMS++ SDDPBlock](https://gitlab.com/smspp/sddpblock)

- [SMS++ UCBlock](https://gitlab.com/smspp/ucblock)

- [SMS++ TwoStageStochasticBlock](https://gitlab.com/smspp/twostagestochasticblock)

### Build and install with CMake

Configure and build the library with:

```sh
mkdir build
cd build
cmake ..
cmake --build .
```

The library has the same configuration options of
[SMS++](https://gitlab.com/smspp/smspp-project/-/wikis/Customize-the-configuration).

Optionally, install the library in the system with:

```sh
cmake --install .
```

### Usage with CMake

After the library is built, you can use it in your CMake project with:

```cmake
find_package(InvestmentBlock)
target_link_libraries(<my_target> SMS++::InvestmentBlock)
```

### Build and install with makefiles

Carefully hand-crafted makefiles have also been developed for those unwilling
to use CMake. Makefiles build the executable in-source (in the same directory
tree where the code is) as opposed to out-of-source (in the copy of the
directory tree constructed in the build/ folder) and therefore it is more
convenient when having to recompile often, such as when developing/debugging
a new module, as opposed to the compile-and-forget usage envisioned by CMake.

Each executable using `InvestmentBlock` has to include a "main makefile" of
the module, which typically is either [makefile-c](makefile-c) including all
necessary libraries comprising the "core SMS++" one, or
[makefile-s](makefile-s) including all necessary libraries but not the "core
SMS++" one (for the common case in which this is used together with other
modules that already include them). One relevant case is the
[InvestmentBlock main in the tools/ repo](https://gitlab.com/smspp/tools/-/blob/develop/investmentblock_solver/investmentblock_solver.cpp?ref_type=heads). The makefiles
in turn recursively include all the required other makefiles, hence one should
only need to edit the "main makefile" for compilation type (C++ compiler and
its options) and it all should be good to go. In case some of the external
libraries are not at their default location, it should only be necessary to
create the `../extlib/makefile-paths` out of the
`extlib/makefile-default-paths-*` for your OS `*` and edit the relevant bits
(commenting out all the rest).

Check the [SMS++ installation wiki](https://gitlab.com/smspp/smspp-project/-/wikis/Customize-the-configuration#location-of-required-libraries)
for further details.


## Tests

The [test](test) folder contains `InvestmentBlock_unit_test`, which checks
the curvature that an `InvestmentFunction` declares and compares its
linearization with the finite differences of its value on small `UCBlock`.
The cross-module testers are in the
[tests repo](https://gitlab.com/smspp/tests) (`tests/InvestmentBlock/`).


## Data

We provide some data sets that are used, among other things, by some of the
testers of the [test repo](https://gitlab.com/smspp/tests). Since they are
large they are not included in the repo. They are automatically downloaded by
CMake if the test repo is included, but if you are not using CMake to build
the system you need to do it by hand, via

```sh
cd data
wget https://gitlab.com/api/v4/projects/45125867/packages/generic/nc4/latest/nc4.tgz
tar xzvf nc4.tgz
```

This builds the [data/nc4](data/nc4) folder with the `InvestmentBlock`
instances (such as the `pypsa-data` set) used by the tests.


## Getting help

If you need support, you want to submit bugs or propose a new feature, you can
[open a new issue](https://gitlab.com/smspp/investmentblock/-/issues/new).


## Contributing

Please read [CONTRIBUTING.md](CONTRIBUTING.md) for details on our code of
conduct, and the process for submitting merge requests to us.


## Authors

### Current Lead Authors

- **Rafael Durbano Lobato**  
  Dipartimento di Informatica  
  Università di Pisa

- **Donato Meoli**  
  Dipartimento di Informatica  
  Università di Pisa

- **Francesco Secoli**  
  Dipartimento di Informatica  
  Università di Pisa

### Contributors

- **Antonio Frangioni**  
  Dipartimento di Informatica  
  Università di Pisa


## License

This code is provided free of charge under the [GNU Lesser General Public
License version 3.0](https://opensource.org/licenses/lgpl-3.0.html) -
see the [LICENSE](LICENSE) file for details.


## Disclaimer

The code is currently provided free of charge under an open-source license.
As such, it is provided "*as is*", without any explicit or implicit warranty
that it will properly behave or it will suit your needs. The Authors of
the code cannot be considered liable, either directly or indirectly, for
any damage or loss that anybody could suffer for having used it. More
details about the non-warranty attached to this code are available in the
license description file.
