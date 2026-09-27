# InvestmentBlock — tests

The C++ test suite of InvestmentBlock is not in this repository, by choice:
the developers keep it locally, since it departs widely from the cross-module
tests and would need a review cycle of its own. The official cross-module
testers are in the [tests repo](https://gitlab.com/smspp/tests)
(`tests/InvestmentBlock/`).

This folder holds only `BSPar_osimp.txt`, the BundleSolver parameters that the
local suite reads, and a `.gitignore` for the `.nc4` instances. The build
enables CTest and adds this folder only where a `test/CMakeLists.txt` is
present, that is, only where the local suite is.
