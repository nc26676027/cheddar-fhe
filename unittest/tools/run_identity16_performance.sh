#!/usr/bin/env bash
# Paired selected Identity16 latency runs. Correctness is qualified separately.
set -uo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repo_dir"

build_dir=${1:-build}
evidence_dir=${2:-.scratch/identity16-18/execution/performance}
runs=${IDENTITY16_PERF_RUNS:-3}
warmups=${IDENTITY16_PERF_WARMUPS:-5}
repeats=${IDENTITY16_PERF_REPEATS:-30}
mkdir -p "$evidence_dir"

{
  date -Iseconds
  git rev-parse HEAD
  nvcc --version
  nvidia-smi -L
  printf 'runs=%s warmups=%s repeats=%s\n' "$runs" "$warmups" "$repeats"
} > "$evidence_dir/environment.txt"

# Three independent paired runs; joint and ordinary alternate order per run so
# neither route always runs against a colder cache or a busier host.
for run in $(seq 1 "$runs"); do
  order=(joint4434 ordinary)
  if (( run % 2 == 0 )); then order=(ordinary joint4434); fi
  for route in "${order[@]}"; do
    IDENTITY16_PERF=1 \
    IDENTITY16_PERF_ROUTE="$route" \
    IDENTITY16_PERF_WARMUPS="$warmups" \
    IDENTITY16_PERF_REPEATS="$repeats" \
      "$build_dir/unittest/identity16_performance_test" \
        --gtest_filter=Identity16Selected.PublicBootstrapPerformance \
        > "$evidence_dir/run-$run-$route.log" 2>&1
    printf '%s\n' "run $run $route exit $?" >> "$evidence_dir/commands.tsv"
  done
done

python3 unittest/tools/summarize_identity16_performance.py "$evidence_dir"
