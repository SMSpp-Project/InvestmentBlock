#!/usr/bin/env python3
# =============================================================================
# generate_instances.py  --  local dev test fixtures (git-excluded).
# =============================================================================
#
# Builds the two synthetic InvestmentBlock netCDF instances used by the local
# tests, both derived from the only shipped instance with lower bound 0 on the
# design variable (so no bound reformulation, which the multi-component path
# does not support):
#     InvestmentBlock/data/nc4/resilient-data/smspp_2n_2c_1g_1lext.nc
#
#   instance_constrained.nc   (test_constraints / vertical_cut)
#       Base + ONE active implicit linear constraint  0 <= 1*x <= CAP (5000).
#       The unconstrained optimum is x* ~= 6300, so the cap BINDS: the bundle
#       probes infeasible points and emits vertical (feasibility) linearizations,
#       letting the test check that set_weight leaves the vertical cut untouched.
#       Built with pySMSpp (the project's netCDF generator): adds, to the
#       InvestmentBlock group, NumConstraints=1 + Constraints_A
#       (NumConstraints x NumAssets) + Constraints_Lower/UpperBound. Format per
#       InvestmentFunction::deserialize (Constraints_A is 2-D).
#
#   instance_hetero.nc        (test_heterogeneous, test_structure)
#       Base with the inner UCBlock hourly demand scaled by DEMAND_SCALE (0.8);
#       everything else byte-for-byte identical. Loading the base into one
#       component and this into another gives two DIFFERENT InvestmentFunction,
#       so the disaggregated objective v == sum_k w_k F_k(x*) is non-trivial.
#       Demand is scaled DOWN so the inner unit-commitment stays feasible. Built
#       with plain netCDF4 (recursive deep-copy + scaling ActivePowerDemand)
#       because the edit targets a variable INSIDE a nested group: a recursive
#       copy keeps the embedded inner block faithful without a model round-trip.
#
# Run:  python3 generate_instances.py        (writes both into this folder)
# =============================================================================

import sys

import numpy as np
import netCDF4 as nc

# pysmspp is a local, non-installed package living under pySMSpp/.
sys.path.insert(0, "/opt/smspp-project/pySMSpp")
import pysmspp  # noqa: E402

ROOT = "/opt/smspp-project/InvestmentBlock"
SRC = f"{ROOT}/data/nc4/resilient-data/smspp_2n_2c_1g_1lext.nc"
HERE = f"{ROOT}/test"

CAP = 5000.0          # implicit upper bound on x; in (0, unconstrained x* ~6300)
DEMAND_SCALE = 0.8    # < 1 keeps the inner unit-commitment feasible


def make_constrained(dst):
    """Base + one active implicit constraint 0 <= x <= CAP, via pySMSpp."""
    net = pysmspp.SMSNetwork(SRC)
    ib = net.blocks["InvestmentBlock"]
    ib.add_dimension("NumConstraints", 1, force=True)
    ib.add_variable("Constraints_A", "double", ("NumConstraints", "NumAssets"),
                    np.array([[1.0]]), force=True)
    ib.add_variable("Constraints_LowerBound", "double", ("NumConstraints",),
                    np.array([0.0]), force=True)
    ib.add_variable("Constraints_UpperBound", "double", ("NumConstraints",),
                    np.array([CAP]), force=True)
    net.to_netcdf(dst, force=True)
    print("wrote", dst, "with implicit constraint 0 <= x <=", CAP)


def _copy_group(src, dst):
    """Faithful recursive copy of a netCDF group (dims, attrs, vars, subgroups)."""
    for name, dim in src.dimensions.items():
        dst.createDimension(name, None if dim.isunlimited() else len(dim))
    dst.setncatts({k: src.getncattr(k) for k in src.ncattrs()})
    for name, var in src.variables.items():
        out = dst.createVariable(name, var.datatype, var.dimensions)
        out.setncatts({k: var.getncattr(k) for k in var.ncattrs()})
        out[:] = var[:]
    for name, grp in src.groups.items():
        _copy_group(grp, dst.createGroup(name))


def make_hetero(dst):
    """Base with the inner ActivePowerDemand scaled by DEMAND_SCALE, via netCDF4."""
    src = nc.Dataset(SRC, "r")
    out = nc.Dataset(dst, "w", format="NETCDF4")
    _copy_group(src, out)
    dem = out.groups["InvestmentBlock"].groups["InnerBlock"] \
             .variables["ActivePowerDemand"]
    dem[:] = dem[:] * DEMAND_SCALE
    src.close()
    out.close()
    print("wrote", dst, "with inner ActivePowerDemand x", DEMAND_SCALE)


if __name__ == "__main__":
    make_constrained(f"{HERE}/instance_constrained.nc")
    make_hetero(f"{HERE}/instance_hetero.nc")
