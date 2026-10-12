#!/usr/bin/env bash
# run-ctest.sh <build-dir> <platform> <run-label> [VAR=value...] [-- ctest args...]
#
# Runs the conformance suite once with the given environment, writes
# results/<platform>__<run-label>.{xml,json} and records a failure in
# results/.failed instead of stopping, so that every run of a job is reported.
# SHARD=i/n runs only every n-th test starting at test i (the report merges
# the shards of a run back together).  TEST_TIMEOUT overrides the per-test
# time limit (seconds).
set -u
build=$1 platform=$2 label=$3
shift 3
envs=() args=()
while (($#)); do
  case $1 in
    --) shift; args=("$@"); break ;;
    *) envs+=("$1"); shift ;;
  esac
done

out=$PWD/results
mkdir -p "$out"
base="$out/${platform}__${label}"
shard=${SHARD-}
if [ -n "$shard" ]; then
  args+=(-I "${shard%/*},,${shard#*/}")
  base+="__${shard%/*}"
fi
jobs=$( (nproc || sysctl -n hw.ncpu) 2>/dev/null )

start=$(date +%s)
(cd "$build" && env ${envs[@]+"${envs[@]}"} ctest -j "$jobs" --timeout "${TEST_TIMEOUT:-1800}" \
  --output-on-failure --output-junit "$base.xml" ${args[@]+"${args[@]}"})
status=$?
end=$(date +%s)

python3 - "$base.json" "$platform" "$label" "$status" "$((end - start))" \
  "${envs[*]-}" "${args[*]-}" "$shard" <<'EOF'
import json, sys
path, platform, label, status, secs, env, args, shard = sys.argv[1:]
json.dump({"platform": platform, "run": label, "exit": int(status),
           "seconds": int(secs), "env": env, "ctest_args": args,
           "shard": shard}, open(path, "w"))
EOF
[ "$status" -eq 0 ] || echo "$platform $label" >> "$out/.failed"
exit 0
