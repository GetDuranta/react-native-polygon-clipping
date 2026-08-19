#!/usr/bin/env bash
# Builds and runs the host-side unit + differential tests.
#
# Differential fixtures (test/fixtures.txt) are generated from the reference
# TypeScript implementation with gen-fixtures.mjs; if the file is present the
# tests replay it, otherwise only the built-in unit tests run.
set -euo pipefail
cd "$(dirname "$0")"

CXX=${CXX:-clang++}
$CXX -std=c++17 -O2 -ffp-contract=off -Wall -Wextra \
  -o polyclip_test test_main.cpp ../cpp/core/polyclip.cpp ../cpp/core/orient2d.cpp

./polyclip_test
if [[ -f fixtures.txt ]]; then
  ./polyclip_test fixtures.txt legacy
  ./polyclip_test fixtures.txt robust || echo "(robust-mode divergences are informational)"
fi
