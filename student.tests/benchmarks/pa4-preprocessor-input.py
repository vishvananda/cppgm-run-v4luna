#!/usr/bin/env python3
"""Generate the fixed, macro-heavy PA4 preprocessing workload."""

import argparse


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output")
    parser.add_argument("--count", type=int, default=160000)
    args = parser.parse_args()

    with open(args.output, "w", encoding="utf-8", newline="\n") as output:
        output.write("#define ID(x) x\n")
        output.write("#define ADD(a, b) ((a) + (b))\n")
        output.write("#define TWICE(x) ID(x) ID(x)\n")
        for index in range(args.count):
            left = index % 997
            right = (index * 37 + 11) % 991
            output.write("TWICE(ADD(%d, %d))\n" % (left, right))


if __name__ == "__main__":
    main()
