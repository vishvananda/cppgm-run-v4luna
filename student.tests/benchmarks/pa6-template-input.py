#!/usr/bin/env python3
"""Generate a fixed type/template and control-flow frontend workload for PA6."""

import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--count", type=int, default=1200)
    args = parser.parse_args()
    if args.count < 1:
        parser.error("--count must be positive")

    with args.output.open("w", encoding="ascii", newline="\n") as out:
        for index in range(args.count):
            suffix = f"{index:05d}"
            out.write(
                f"template<class T> struct Record{suffix} {{\n"
                "  T value;\n"
                "  T transform(T argument) { T local = argument; return local; }\n"
                "};\n"
                f"template<class T> T identity{suffix}(T argument) {{ return argument; }}\n"
                f"int kernel{suffix}(int seed) {{\n"
                "  int sum = seed;\n"
                "  for (int i = 0; i < 3; ++i) { sum += i; }\n"
                "  if (sum > 2) { return sum; }\n"
                "  return 0;\n"
                "}\n"
            )


if __name__ == "__main__":
    main()
