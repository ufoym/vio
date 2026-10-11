#!/usr/bin/env bash
# run-ctest.sh <build-dir> <platform> <run-label> [VAR=value...] [-- ctest args...]
#
# Runs the conformance suite once with the given environment, writes
# results/<platform>__<run-label>.{xml,json} and records a failure in
# results/.failed instead of stopping, so that every run of a job is reported.
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
jobs=$( (nproc || sysctl -n hw.ncpu) 2>/dev/null )

start=$(date +%s)
(cd "$build" && env ${envs[@]+"${envs[@]}"} ctest -j "$jobs" --timeout 1800 \
  --output-on-failure --output-junit "$base.xml" ${args[@]+"${args[@]}"})
status=$?
end=$(date +%s)

python3 - "$base.json" "$platform" "$label" "$status" "$((end - start))" \
  "${envs[*]-}" "${args[*]-}" <<'EOF'
import json, sys
path, platform, label, status, secs, env, args = sys.argv[1:]
json.dump({"platform": platform, "run": label, "exit": int(status),
           "seconds": int(secs), "env": env, "ctest_args": args},
          open(path, "w"))
EOF
[ "$status" -eq 0 ] || echo "$platform $label" >> "$out/.failed"
exit 0
