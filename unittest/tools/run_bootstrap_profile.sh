#!/usr/bin/env bash
# Preserve independent profiling evidence; GPU work runs serially.
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repo_dir"
evidence_dir=${1:-.scratch/meboot-performance/bootstrap-profile/final}
mkdir -p "$evidence_dir"
{
  date -Iseconds
  git rev-parse HEAD
  nvcc --version
  cmake --version
  nvidia-smi
} > "$evidence_dir/environment.txt"
git diff --binary > "$evidence_dir/source.patch"
find include src unittest parameters -type f -print0 | sort -z |
  xargs -0 sha256sum > "$evidence_dir/source.sha256"
cmake --build build --target meboot_performance_test -j 4 > "$evidence_dir/build.log" 2>&1
printf '%s\n' 'CHEDDAR_BENCH_REPEATS=30 CHEDDAR_BOOT_PROFILE=1 build/unittest/meboot_performance_test' > "$evidence_dir/command.txt"
set +e
CHEDDAR_BENCH_REPEATS=30 CHEDDAR_BOOT_PROFILE=1 build/unittest/meboot_performance_test \
  --gtest_output="xml:$evidence_dir/meboot_performance_test.xml" \
  > "$evidence_dir/meboot_performance_test.log" 2>&1
status=$?
set -e
printf '%s\n' "$status" > "$evidence_dir/exit-code.txt"
tail -n 8 "$evidence_dir/meboot_performance_test.log"
if [[ "$status" != 0 ]]; then exit "$status"; fi
python3 unittest/tools/summarize_meboot_performance.py \
  "$evidence_dir/meboot_performance_test.log" --require-targets > "$evidence_dir/performance-summary.txt"
python3 unittest/tools/summarize_bootstrap_profile.py \
  "$evidence_dir/meboot_performance_test.log" > "$evidence_dir/profile-summary.txt"
