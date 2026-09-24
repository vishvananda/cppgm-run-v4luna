#!/usr/bin/env python3
"""Generate a fixed PA6-valid workload for array completion scaling."""

import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--count", type=int, default=8000)
    args = parser.parse_args()
    if args.count < 1:
        parser.error("--count must be positive")

    with args.output.open("w", encoding="ascii", newline="\n") as out:
        for index in range(args.count):
            name = f"array_{index:05d}"
            out.write(f"extern int {name}[];\nint {name}[4];\n")


if __name__ == "__main__":
    main()
