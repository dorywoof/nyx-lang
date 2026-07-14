#!/usr/bin/env python3
"""Runs each nyx/python benchmark pair several times and reports the median
in-process TIME each script prints on its own last-but-one line. Wall clock
around the whole subprocess is also recorded for transparency, but the
TIME: figure (measured with clock()/time.perf_counter() inside the script)
is what's used for the comparison table, since it excludes interpreter
startup and subprocess overhead on both sides equally.

Usage: bench.py <path-to-nyx-binary> [runs-per-benchmark]
"""
import statistics
import subprocess
import sys
import time
from pathlib import Path

BENCH_DIR = Path(__file__).parent
PAIRS = [
    ("fib", "fib.nyx", "fib.py"),
    ("sort", "sort.nyx", "sort.py"),
    ("strings", "strings.nyx", "strings.py"),
]


def parse_time(output: str) -> float:
    for line in output.splitlines():
        if line.startswith("TIME:"):
            return float(line[len("TIME:") :])
    raise ValueError(f"no TIME: line in output:\n{output}")


def run_once(cmd: list[str]) -> tuple[float, str]:
    t0 = time.perf_counter()
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    wall = time.perf_counter() - t0
    if result.returncode != 0:
        raise RuntimeError(f"{cmd} failed:\n{result.stdout}\n{result.stderr}")
    return wall, result.stdout


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: bench.py <nyx-binary> [runs]", file=sys.stderr)
        return 2

    nyx_binary = sys.argv[1]
    runs = int(sys.argv[2]) if len(sys.argv) > 2 else 5

    rows = []
    for label, nyx_script, py_script in PAIRS:
        nyx_times, py_times = [], []
        for _ in range(runs):
            _, out = run_once([nyx_binary, str(BENCH_DIR / nyx_script)])
            nyx_times.append(parse_time(out))
            _, out = run_once([sys.executable, str(BENCH_DIR / py_script)])
            py_times.append(parse_time(out))

        nyx_median = statistics.median(nyx_times)
        py_median = statistics.median(py_times)
        ratio = py_median / nyx_median if nyx_median > 0 else float("inf")
        rows.append((label, nyx_median, py_median, ratio))
        print(
            f"{label}: nyx={nyx_median*1000:.1f}ms  python={py_median*1000:.1f}ms  "
            f"(python/nyx = {ratio:.2f}x)"
        )

    print("\n| Benchmark | Nyx (median, {0} runs) | CPython 3 (median) | Ratio (Python / Nyx) |".format(runs))
    print("|---|---|---|---|")
    for label, nyx_median, py_median, ratio in rows:
        faster = "nyx faster" if ratio > 1 else "python faster"
        print(f"| {label} | {nyx_median*1000:.1f} ms | {py_median*1000:.1f} ms | {ratio:.2f}x ({faster}) |")

    return 0


if __name__ == "__main__":
    sys.exit(main())
