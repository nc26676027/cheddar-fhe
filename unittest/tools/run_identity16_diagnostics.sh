#!/usr/bin/env bash
# Exact selected diagnostics; does not qualify performance.
set -uo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repo_dir"
export IDENTITY16_STAGE_ORACLE="$repo_dir/.scratch/identity16-18/execution/reference-stage-oracle.json"
build_dir=${1:-build/identity16-native}
evidence_dir=${2:-.scratch/identity16-18/execution/reproduce}
mkdir -p "$evidence_dir"
python3 unittest/tools/import_identity16_profiles.py || exit "$?"
python3 unittest/tools/identity16_lcr_bound.py > "$evidence_dir/lcr-bound.json" || exit "$?"
{
  date -Iseconds
  git rev-parse HEAD
  git status --short
  uname -a
  nvcc --version
  cmake --version
  nvidia-smi
} > "$evidence_dir/environment.txt"
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CUDA_ARCHITECTURES=120 -DFETCHCONTENT_UPDATES_DISCONNECTED=ON \
  > "$evidence_dir/configure.log" 2>&1 || exit "$?"
cmake --build "$build_dir" -j 4 > "$evidence_dir/build.log" 2>&1 || exit "$?"
find include src unittest parameters -type f -print0 | sort -z |
  xargs -0 sha256sum > "$evidence_dir/source.sha256"
git diff --binary > "$evidence_dir/source.patch"
sha256sum "$build_dir/libcheddar.so" "$build_dir/unittest/identity16_test" \
  > "$evidence_dir/binaries.sha256"
: > "$evidence_dir/commands.tsv"
failed=0
for target in identity16_test centered_crt_test low_q0p_entry_test \
              basic_test user_interface_test mod_up_to_max_test \
              meboot_matrix_planner_test aks_key_test lcr_step_test \
              meboot_cts_test linear_transform_test boot_test; do
  "$build_dir/unittest/$target" --gtest_output="xml:$evidence_dir/$target.xml" \
    > "$evidence_dir/$target.log" 2>&1
  status=$?
  printf '%s\t%s\n' "$target" "$status" >> "$evidence_dir/commands.tsv"
  printf '%s: exit %s\n' "$target" "$status"
  if [[ "$status" != 0 ]]; then failed=1; fi
done
printf '%s\n' 'Performance gate: NOT MEASURED by this diagnostic runner.' \
  > "$evidence_dir/performance-status.txt"
exit "$failed"
