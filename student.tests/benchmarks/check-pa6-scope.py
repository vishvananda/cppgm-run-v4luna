#!/usr/bin/env python3
"""Check PA6 facts that are not observable in its full-dump fixtures."""

import subprocess
import sys
import tempfile
from pathlib import Path


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: check-pa6-scope.py CPPGM SOURCE")
    compiler = Path(sys.argv[1]).resolve()
    source = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="pa6-scope-check-") as directory:
        output = Path(directory) / "types"
        subprocess.run(
            [str(compiler), "--emit-types", "-o", str(output), str(source)],
            check=True,
        )
        text = output.read_text()
    if text.count("scope class Local\n") != 2:
        raise SystemExit("expected separate local class scopes for sibling blocks")
    for required in (
        "variable first int",
        "variable second char",
        "variable completed array of 4 int",
        "scope class FirstTemplate",
        "scope class SecondTemplate",
        "function cv_parameter function of (const int) returning void",
        "function cv_parameter function of (int) returning void",
        "function array_parameter function of (array of 4 int) returning void",
        "function array_parameter function of (pointer to int) returning void",
        "function callback_parameter function of (function of (int) returning void) returning void",
        "function callback_parameter function of (pointer to function of (int) returning void) returning void",
    ):
        if required not in text:
            raise SystemExit(f"missing PA6 semantic fact: {required}")
    print("PA6 scope and completion checks passed")


if __name__ == "__main__":
    main()
