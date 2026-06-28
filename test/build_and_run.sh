#!/usr/bin/env bash
# build_and_run.sh — build and run every test_*.cpp in this folder by
# reusing the EXACT compile/link recipe of the umbrella's InvestmentBlock_test
# target (find_package is unusable: StochasticBlock pulls StOpt, which has no
# CMake package config).
set -uo pipefail   # NOT -e: a failing test must not abort the whole suite

ROOT=/opt/smspp-project
BUILD=$ROOT/build
HERE=$ROOT/InvestmentBlock/test
DIR=$BUILD/tests/InvestmentBlock/CMakeFiles/InvestmentBlock_test.dir
CPLEX_BIN=/var/tmp/smspp/ibm/ILOG/CPLEX_Studio/cplex/bin/x86-64_linux
mkdir -p "$HERE/build"

export LD_LIBRARY_PATH="$BUILD/MILPSolver:$BUILD/InvestmentBlock:$BUILD/BundleSolver:\
$BUILD/UCBlock:$BUILD/SDDPBlock:$BUILD/SMS++:$ROOT/lib:$CPLEX_BIN:${LD_LIBRARY_PATH:-}"

[ -f "$DIR/flags.make" ] || { echo "build InvestmentBlock_test first (umbrella)"; exit 1; }

CXX_DEFINES=$(sed -n 's/^CXX_DEFINES = //p' "$DIR/flags.make")
CXX_INCLUDES=$(sed -n 's/^CXX_INCLUDES = //p' "$DIR/flags.make")
CXX_FLAGS=$(sed -n 's/^CXX_FLAGS = //p' "$DIR/flags.make")

build_one() {  # $1 = test name (no extension)
  local name=$1
  # shellcheck disable=SC2086
  c++ $CXX_DEFINES $CXX_INCLUDES $CXX_FLAGS \
      -c "$HERE/$name.cpp" -o "$HERE/build/$name.o" || return 1
  ( cd "$BUILD/tests/InvestmentBlock" && \
    sed -e "s#CMakeFiles/InvestmentBlock_test.dir/test.cpp.o#$HERE/build/$name.o#g" \
        -e "s#-o InvestmentBlock_test#-o $HERE/build/$name#" \
        -e "s#-Wl,--dependency-file=[^ ]*##" \
        "$DIR/link.txt" | bash )
}

pass=0; fail=0
for src in "$HERE"/test_*.cpp; do
  name=$(basename "$src" .cpp)
  echo "==================== $name ===================="
  if ! build_one "$name"; then
    echo "  BUILD FAIL"; fail=$((fail+1)); continue
  fi
  ( cd "$ROOT/tests/InvestmentBlock" && "$HERE/build/$name" )
  if [ $? -eq 0 ]; then pass=$((pass+1)); else fail=$((fail+1)); fi
done

echo "================================================"
echo "suite: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
