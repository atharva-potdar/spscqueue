#!/usr/bin/env python3
"""Summarize bench/run_reps.sh output as median/min/max over repetitions."""

from __future__ import annotations

import pathlib
import shlex
import statistics
import sys


def parse_kv(line: str, prefix: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for part in shlex.split(line[len(prefix) :]):
        key, value = part.split("=", 1)
        out[key] = value
    return out


def read_rows(path: pathlib.Path, prefix: str, key: str) -> dict[str, dict[str, str]]:
    rows: dict[str, dict[str, str]] = {}
    for line in path.read_text().splitlines():
        if line.startswith(prefix):
            row = parse_kv(line, prefix)
            rows[row[key]] = row
    return rows


def median(values: list[float]) -> float:
    return statistics.median(values)


def ordered_keys(runs: list[dict[str, dict[str, str]]]) -> list[str]:
    keys: list[str] = []
    for run in runs:
        for key in run:
            if key not in keys:
                keys.append(key)
    return keys


def environment(latency_files: list[pathlib.Path]) -> list[str]:
    """Pull the header lines the harness prints before the first RESULT row."""
    lines: list[str] = []
    for line in latency_files[0].read_text().splitlines():
        if line.startswith("RESULT_"):
            break
        stripped = line.strip()
        if not stripped or ":" not in stripped:
            continue
        lines.append(stripped)
    for line in latency_files[0].read_text().splitlines():
        if line.startswith("ENV "):
            lines.append(line.strip())
    return lines


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <results-dir>", file=sys.stderr)
        return 2

    directory = pathlib.Path(sys.argv[1])
    latency_files = sorted(directory.glob("latency-*.txt"))
    throughput_files = sorted(directory.glob("throughput-*.txt"))
    if not latency_files or not throughput_files:
        print(f"no latency/throughput files in {directory}", file=sys.stderr)
        return 1

    latencies = [read_rows(p, "RESULT_LAT ", "name") for p in latency_files]
    throughputs = [read_rows(p, "RESULT_TP ", "name") for p in throughput_files]

    print(f"# Benchmark summary ({len(latency_files)} repetitions)")
    print()
    print(f"Directory: `{directory}`")
    print()
    print("## Environment")
    print()
    for line in environment(latency_files):
        if line.startswith("ENV "):
            line = line[len("ENV ") :]
        print(f"- {line}")
    print()

    print("## Latency (nanoseconds, median across repetitions)")
    print()
    print("| Benchmark | mean | p50 | p99 | p99.9 | p99.9 min..max |")
    print("|---|---:|---:|---:|---:|---:|")
    for name in ordered_keys(latencies):
        rows = [run[name] for run in latencies if name in run]
        if not rows:
            continue
        p999 = [float(r["p999"]) for r in rows]
        print(
            f"| {name} "
            f"| {median([float(r['mean']) for r in rows]):.2f} "
            f"| {median([float(r['p50']) for r in rows]):.2f} "
            f"| {median([float(r['p99']) for r in rows]):.2f} "
            f"| {median(p999):.2f} "
            f"| {min(p999):.2f}..{max(p999):.2f} |"
        )

    print()
    print("## Throughput (enqueue+dequeue pairs, median across repetitions)")
    print()
    print("| Benchmark | Mops/s | min..max | ns/pair |")
    print("|---|---:|---:|---:|")
    for name in ordered_keys(throughputs):
        rows = [run[name] for run in throughputs if name in run]
        if not rows:
            continue
        mops = [float(r["mops"]) for r in rows]
        print(
            f"| {name} "
            f"| {median(mops):.2f} "
            f"| {min(mops):.2f}..{max(mops):.2f} "
            f"| {median([float(r['ns_per_pair']) for r in rows]):.2f} |"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
