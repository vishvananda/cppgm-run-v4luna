#!/usr/bin/env python3
"""Generate a stable template-heavy translation unit for the PA5 parser."""

import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--count", type=int, default=1500)
    args = parser.parse_args()
    if args.count < 1:
        parser.error("--count must be positive")

    with args.output.open("w", encoding="ascii", newline="\n") as out:
        out.write(
            "template<int N> struct Constant { static const int value = N; };\n"
            "template<class T, class U> struct Pair {};\n"
        )
        for i in range(args.count):
            suffix = f"{i:06d}"
            out.write(
                f"template<class T, int N> struct Record{suffix} {{\n"
                "  T payload[N];\n"
                "  T read() const { return payload[0]; }\n"
                "};\n"
                f"Record{suffix}<int, 4> record{suffix};\n"
                f"Constant<(6 > 1 ? 2 : 0)> constant{suffix};\n"
                f"Pair<Record{suffix}<int, 4>, Constant<5> > nested{suffix};\n"
                f"template<class T> T identity{suffix}(T value) {{ return value; }}\n"
                f"int consume{suffix}(int value) {{ return value; }}\n"
                f"int read_memory{suffix}(int* data) {{ return data[0]; }}\n"
                f"int kernel{suffix}(int input) {{\n"
                f"  int value = input + {i % 97};\n"
                "  for (int i = 0; i < 4; ++i) {\n"
                f"    value += consume{suffix}(input + i);\n"
                "  }\n"
                "  if (value > 3) { return value * 2; }\n"
                "  else { return value - 1; }\n"
                "}\n"
                f"float floating{suffix}(float value) {{\n"
                "  return value * 1.25f + 0.5f;\n"
                "}\n"
            )


if __name__ == "__main__":
    main()
