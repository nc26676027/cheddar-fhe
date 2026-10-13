#!/usr/bin/env bash
# Validate the key boundary, normal-chain depth, precision and full Boot profile.
set -euo pipefail
repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$repo_dir"
evidence_dir=${1:-.scratch/meboot-performance/modulus-alignment/validated}
bash unittest/tools/run_meboot_quality.sh "$evidence_dir/regression"
mkdir -p "$evidence_dir/extra"
build/unittest/basic_test \
  '--gtest_filter=*EncodeEncryptDecryptDecode*:*HMult*:*HRot*:*HConj*:*AutonomousModSwitchOperations*' \
  --gtest_output="xml:$evidence_dir/extra/basic.xml" \
  > "$evidence_dir/extra/basic.log" 2>&1
printf '%s\n' 'basic normal-chain encryption, multiplication, rotation, conjugation, mixed-base switching: exit 0'
CHEDDAR_BENCH_REPEATS=11 build/unittest/stc_tuning_test \
  --gtest_output="xml:$evidence_dir/extra/stc.xml" \
  > "$evidence_dir/extra/stc.log" 2>&1
printf '%s\n' 'StC exact differential at the aligned levels: exit 0'
bash unittest/tools/run_bootstrap_profile.sh "$evidence_dir/profile"
