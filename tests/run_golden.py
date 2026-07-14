#!/usr/bin/env python3
"""Golden-file test runner for the nyx interpreter.

For every tests/lang/*.nyx script, runs `nyx <script>.nyx` and compares its
stdout byte-for-byte against the matching `<script>.expected` file.
Usage: run_golden.py <path-to-nyx-binary> <lang-dir>
"""
import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_golden.py <nyx-binary> <lang-dir>", file=sys.stderr)
        return 2

    nyx_binary = sys.argv[1]
    lang_dir = Path(sys.argv[2])

    scripts = sorted(lang_dir.glob("*.nyx"))
    if not scripts:
        print(f"no .nyx scripts found in {lang_dir}", file=sys.stderr)
        return 2

    failures = 0
    for script in scripts:
        expected_path = script.with_suffix(".expected")
        if not expected_path.exists():
            print(f"SKIP {script.name}: no .expected file")
            continue

        expected = expected_path.read_text()
        result = subprocess.run(
            [nyx_binary, str(script)], capture_output=True, text=True, timeout=30
        )

        if result.stdout != expected:
            failures += 1
            print(f"FAIL {script.name}")
            print(f"  --- expected ---\n{expected}")
            print(f"  --- actual ---\n{result.stdout}")
            if result.stderr:
                print(f"  --- stderr ---\n{result.stderr}")
        else:
            print(f"PASS {script.name}")

    total = len(scripts)
    print(f"\n{total - failures}/{total} golden tests passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
