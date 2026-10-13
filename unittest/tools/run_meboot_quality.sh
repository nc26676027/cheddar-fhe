#!/usr/bin/env bash
# Preserve earlier acceptance evidence and run final relevant regressions.
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repo_dir"
evidence_dir=${1:-.scratch/meboot-performance/final}
mkdir -p "$evidence_dir"
python3 unittest/tools/audit_meboot_modulus.py > "$evidence_dir/modulus-audit.json"
{
  date -Iseconds
  git rev-parse HEAD
  nvcc --version
  cmake --version
  nvidia-smi
} > "$evidence_dir/environment.txt"
git diff --binary > "$evidence_dir/source.patch"
find include src unittest parameters -type f -print0 | sort -z | \
  xargs -0 sha256sum > "$evidence_dir/source.sha256"
sha256sum CMakeLists.txt >> "$evidence_dir/source.sha256"
cmake --build build -j 4 > "$evidence_dir/build.log" 2>&1
: > "$evidence_dir/commands.tsv"
failed=0
for target in user_interface_test centered_crt_test mod_up_to_max_test \
              eval_mod_range_test \
              meboot_matrix_planner_test aks_key_test lcr_step_test \
              meboot_cts_test linear_transform_test boot_test meboot_performance_test; do
  command=("build/unittest/$target" "--gtest_output=xml:$evidence_dir/$target.xml")
  printf '%q ' "${command[@]}" > "$evidence_dir/$target.command"
  printf '\n' >> "$evidence_dir/$target.command"
  if CHEDDAR_BENCH_REPEATS=30 "${command[@]}" > "$evidence_dir/$target.log" 2>&1; then
    status=0
  else
    status=$?
    failed=1
  fi
  printf '%s\t%s\n' "$target" "$status" >> "$evidence_dir/commands.tsv"
  printf '%s: exit %s\n' "$target" "$status"
  tail -n 5 "$evidence_dir/$target.log"
done
if [[ "$failed" == 0 ]]; then
  python3 unittest/tools/summarize_meboot_performance.py \
    "$evidence_dir/meboot_performance_test.log" --require-targets \
    > "$evidence_dir/performance-summary.txt"
fi
exit "$failed"
