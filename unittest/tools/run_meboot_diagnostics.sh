#!/usr/bin/env bash
# Run the frozen selected route and ordinary regression with its pinned oracle.
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repo_dir"
export MEBOOT_STAGE_ORACLE="$repo_dir/unittest/fixtures/identity19-stage-oracle.json.gz"
build_dir=${1:-build}
evidence_dir=${2:-build/validation/meboot-diagnostics}
mkdir -p "$evidence_dir"
{
  date -Iseconds
  git rev-parse HEAD
  git status --short
  uname -a
  nvcc --version
  cmake --version
  nvidia-smi
} > "$evidence_dir/environment.txt"
sha256sum "$MEBOOT_STAGE_ORACLE" parameters/identity19_meboot.json \
  parameters/ordinary.json > "$evidence_dir/inputs.sha256"
cmake --build "$build_dir" -j 4 --target meboot_test > "$evidence_dir/build.log" 2>&1
"$build_dir/unittest/meboot_test" \
  --gtest_output="xml:$evidence_dir/meboot_test.xml" > "$evidence_dir/meboot_test.log" 2>&1
