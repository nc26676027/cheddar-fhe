#!/usr/bin/env bash
# Run from any directory in Linux/WSL; keep GPU tests serial.
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repo_dir"
build_dir=${1:-build}
evidence_dir=${2:-.scratch/phase1-acceptance/logs}
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
git diff --binary > "$evidence_dir/source.patch"
sha256sum parameters/meboot_golden_4434.bin.gz > "$evidence_dir/golden.sha256"
find include src unittest parameters -type f -print0 | sort -z | \
  xargs -0 sha256sum > "$evidence_dir/source.sha256"
sha256sum CMakeLists.txt >> "$evidence_dir/source.sha256"
cmake -S . -B "$build_dir" -DFETCHCONTENT_UPDATES_DISCONNECTED=ON \
  > "$evidence_dir/configure.log" 2>&1
cmake --build "$build_dir" -j 4 > "$evidence_dir/build.log" 2>&1
: > "$evidence_dir/commands.tsv"
failed=0
for target in meboot_matrix_planner_test aks_key_test lcr_step_test \
              meboot_cts_test linear_transform_test basic_test boot_test; do
  command=("$build_dir/unittest/$target" "--gtest_output=xml:$evidence_dir/$target.xml")
  if [[ "$target" == basic_test ]]; then
    command+=("--gtest_filter=*AutonomousModSwitchOperations*")
  fi
  printf '%q ' "${command[@]}" > "$evidence_dir/$target.command"
  printf '\n' >> "$evidence_dir/$target.command"
  if "${command[@]}" > "$evidence_dir/$target.log" 2>&1; then
    status=0
  else
    status=$?
    failed=1
  fi
  printf '%s\t%s\n' "$target" "$status" >> "$evidence_dir/commands.tsv"
  printf '%s: exit %s\n' "$target" "$status"
  tail -n 5 "$evidence_dir/$target.log"
done
exit "$failed"
