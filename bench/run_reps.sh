#!/usr/bin/env bash
# Runs the latency and throughput harnesses REPS times and writes raw output
# plus a median summary.
#
# Each harness pins its own producer/consumer threads; CPU only narrows the set
# of cores the harness is allowed to choose from. Leave it empty to use every
# online core.
#
# Usage:
#   REPS=5 bench/run_reps.sh
#   CPU=2,3,4,10 REPS=5 bench/run_reps.sh
#   BUILD=build/release bench/run_reps.sh
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD="${BUILD:-build}"
CPU="${CPU:-}"
REPS="${REPS:-5}"
OUT="${OUT:-bench/results/$(date +%Y%m%d-%H%M%S)}"

cmake --build "$BUILD" -j "$(nproc)"

mkdir -p "$OUT"
echo "Writing results to $OUT (reps=$REPS, cpus=${CPU:-all})"

run_pinned() {
  if [ -n "$CPU" ]; then
    taskset -c "$CPU" "$@"
  else
    "$@"
  fi
}

for i in $(seq 1 "$REPS"); do
  echo "rep $i/$REPS: latency"
  run_pinned "$BUILD/bench_latency" >"$OUT/latency-$i.txt" 2>&1
  echo "rep $i/$REPS: throughput"
  run_pinned "$BUILD/bench_throughput" >"$OUT/throughput-$i.txt" 2>&1
done

if [ -x "$BUILD/spscqueue" ]; then
  echo "Google Benchmark cross-check"
  run_pinned "$BUILD/spscqueue" --benchmark_min_time=0.2s \
    --benchmark_repetitions=5 >"$OUT/gbench.txt" 2>&1
fi

python3 bench/summarize.py "$OUT" | tee "$OUT/summary.md"
