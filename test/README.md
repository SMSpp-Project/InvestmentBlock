# InvestmentBlock — disaggregated (1:K) tests

Extra C++ tests for a new mode of the InvestmentBlock — solving it as several
weighted **components** handed to the solver *separately*, instead of as one
function. These tests are **still under development**, which is why they live here
and not yet alongside the official suite in `tests/InvestmentBlock/` (that suite
runs with `ctest -L InvestmentBlock` and only checks the old single-function path).

## Background — what this is about

The InvestmentBlock decides **how much to invest** in some assets; these amounts
are the *design variables*, written `x`. For a given `x`, an inner energy model
(a UCBlock) computes the resulting operating cost `F(x)`. The goal is to pick the
`x` that minimises investment cost plus `F(x)`; the best `x` found is called `x*`.

The new **disaggregated (1:K)** mode splits that single cost `F` into K pieces
`F_1, …, F_K` — for example one per scenario, or one per time period. The key
point: the pieces are given to the `BundleSolver` as **K separate components**
(one sub-Block each), **not** as one pre-summed function. The solver keeps a
separate model for each component and minimises their weighted sum
`w_1·F_1 + … + w_K·F_K`, where each weight `w_k` says how much that piece counts
(e.g. a scenario's probability). Keeping the components separate is the goal of
this work; the old code used a single function (K=1), the baseline these tests
compare against. "1:K" means *one* InvestmentBlock with *K* **components**.

Solving happens on two levels:

- a **master** optimises the outer investment problem (over `x`). Two are used
  here: **QPP** (QPPenaltyMP), which only works for a single function (K=1), and
  **OSiMP**, which can handle a real sum of components (K ≥ 2);
- an **inner** solver evaluates each `F_k(x)` (the UCBlock energy problem).

## What is in this folder

- `test_equivalence.cpp`, `test_heterogeneous.cpp`, `test_constraints.cpp`,
  `test_structure.cpp` — the four tests (described below).
- `investment_test_common.h` — small shared helpers (open an instance, read the
  design variables, build and solve a block).
- `generate_instances.py` — builds the two `.nc` test instances.
- `instance_constrained.nc`, `instance_hetero.nc` — the generated instances.
- `BSPar_osimp.txt` — settings for the master solver used when K ≥ 2.
- `build_and_run.sh` — builds and runs every test in this folder.

## What each test checks

Each test targets one property of the disaggregated code, and a concrete mistake
it would catch.

- **test_equivalence** — the disaggregated sum must reach the *same* optimum as the
  old single-function path.
  - *Why:* if `set_weight` scaled the objective value but not the linearizations
    (the local model the bundle builds at each step), the value would look right
    while the solver converged to the wrong point. Checking the solution `x*`, not
    only the value, is what catches this.
  - *How:* solve the legacy block once (reference value and `x*`), then build K
    identical weighted components over the same data, solve, and check three things:
    value == `(sum of weights) × legacy value`, solution == legacy `x*`, and the
    block really has K component sub-Blocks. A positive scale does not move the
    minimiser, so `x*` must stay put while only the value scales. K=1 runs on the
    cheap QPP master, K=2/3/4 on OSiMP.

- **test_heterogeneous** — the first test where the components are genuinely
  different, so the optimum is a real compromise between them, not just the legacy
  point.
  - *Why:* it guards against a wrong weighting, or a wrong sum over different
    components — mistakes that identical-component tests cannot see.
  - *How:* component 0 is the base instance, component 1 the same instance with the
    inner demand ×0.8, so `F_0 ≠ F_1`. It solves `2·F_0 + 3·F_1`, reads `x*`,
    re-evaluates each component at `x*`, and checks the master objective equals
    `sum_k w_k F_k(x*)` (and that `F_0 ≠ F_1`, so the test is really heterogeneous).
    It runs once with `x` free and once with `x` fixed at 0; the fixed case has no
    bundle convergence, so it is a clean, deterministic check.

- **test_constraints** — the only test for the two new features, the asset→variable
  mapping and the implicit linking constraints. Both need the OSiMP master: a sum of
  components plus active feasibility cuts, which the QPP master cannot handle.
  - *Mapping:* T=3 components share the same T design variables, but each invests
    only in its own period. With identical components the optimum must be
    `x_0 = x_1 = x_2 =` the single-period optimum; if the mapping were ignored every
    component would read variable 0 and the others would carry no cost — so this is a
    strong check that the mapping works.
  - *Constraints:* per-period caps that decrease over time make the free optimum
    decreasing; adding `x^(t+1) ≥ x^(t)` must force the solution back to a
    non-decreasing one.
  - *Vertical cut:* an active bound `0 ≤ x ≤ 5000` (the free optimum ~6300 binds)
    makes the solver probe infeasible points and add "vertical" feasibility cuts.
    Across weighted decompositions the solution must stay at 5000 and the value scale
    by the sum of weights — proof that `set_weight` does *not* move the feasible
    region.

- **test_structure** — no optimisation; it checks two framework rules.
  - *Why:* the block must fail loudly instead of silently building a broken object;
    and changes must propagate correctly — `set_value` sends no notification (it is
    solution data), but a real change (`is_fixed`) does, and notifications travel
    *up* the block tree to the master.
  - *How:* it calls `add_component` with bad arguments (no design variables, a null
    component, a non-positive weight, bound reformulation with several components)
    and checks each one throws; then it puts a counting mock solver on the master,
    fixes a shared variable and checks the notification arrives, and finally moves
    the variable and checks every component recomputes (no stale value).

## How to run

1. Build the umbrella `InvestmentBlock_test` target once. The tests reuse its
   compiler and linker settings:

   ```bash
   cd /opt/smspp-project/build
   cmake --build . --target InvestmentBlock --target InvestmentBlock_test -j4
   ```

2. Build and run every test in this folder:

   ```bash
   cd /opt/smspp-project/InvestmentBlock/test
   bash ./build_and_run.sh
   ```

The script sets the library path and runs each test from `tests/InvestmentBlock/`,
so the short config-file names are found.

## Solvers used

- **Inner problem** (the UCBlock): HiGHS (`BSCfg1.txt`) — free, no licence.
- **Master, K=1**: QPPenaltyMP (`BSPar.txt`) — free, no licence.
- **Master, K ≥ 2**: OSiMP (`BSPar_osimp.txt`). QPPenaltyMP cannot handle a sum of
  components, so K ≥ 2 needs this master (Gurobi backend in this build).
- **Block formulation**: `InnerBCfg.txt`.

(`BSPar.txt`, `BSCfg1.txt` and `InnerBCfg.txt` live in `tests/InvestmentBlock/`.)

## Test instances

Both are built from the base instance
`../data/nc4/resilient-data/smspp_2n_2c_1g_1lext.nc` (official reference #4,
objective `6.626444342e+08`) by running `generate_instances.py`:

- `instance_hetero.nc` — the base instance with the inner demand multiplied by
  0.8, so one component differs from the base.
- `instance_constrained.nc` — the base instance plus one active limit
  `0 ≤ x ≤ 5000`.
