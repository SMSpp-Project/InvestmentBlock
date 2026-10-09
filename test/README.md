# InvestmentBlock — tests

`test.cpp` is the unit test of the module, `InvestmentBlock_unit_test`, with
the Configuration files it reads (`BSCfg.txt`, `IBOCfg.txt`, `IFCfg.txt`).
The official cross-module testers are in the
[tests repo](https://gitlab.com/smspp/tests) (`tests/InvestmentBlock/`).

The developers of this branch keep a wider C++ suite locally, outside this
repository, since it departs widely from the cross-module tests and would
need a review cycle of its own: `CMakeLists.txt` includes its CMake file,
`local.cmake`, where it is present. `BSPar_osimp.txt` holds the BundleSolver
parameters that the local suite reads, and `.gitignore` covers the `.nc4`
instances.
