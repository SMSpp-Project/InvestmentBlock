# InvestmentBlock — tests

The C++ test suite that used to live here was **removed during the
development of the disaggregated (1:K) / multi-period extension**, for
cleanliness: it targeted the old single-function path and no longer matched
the extended class.

A rewritten, self-contained ctest suite (regression, structural, SDDP and
multi-period tiers) is maintained locally by the developers and will be
contributed separately once the extension stabilizes. The official
cross-module testers remain in the
[tests repo](https://gitlab.com/smspp/tests) (`tests/InvestmentBlock/`).
