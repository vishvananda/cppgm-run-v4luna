#!/usr/bin/env python3
"""A/A noise calibration and frozen A/B compiler ABBA for PA6 type dumps."""

import argparse
import hashlib
import subprocess
import time
from pathlib import Path


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def run(binary, input_path, output_path, measurement_path):
    started = time.perf_counter_ns()
    subprocess.run(
        [
            "/usr/bin/time",
            "-f",
            "%M",
            "-o",
            str(measurement_path),
            str(binary),
            "--emit-types",
            "-o",
            str(output_path),
            str(input_path),
        ],
        check=True,
    )
    elapsed = (time.perf_counter_ns() - started) / 1_000_000_000.0
    rss = int(measurement_path.read_text().strip())
    return elapsed, rss, digest(output_path), output_path.stat().st_size


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("A", type=Path)
    parser.add_argument("B", type=Path)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("raw_tsv", type=Path)
    parser.add_argument("--noise-blocks", type=int, default=4)
    parser.add_argument("--abba-blocks", type=int, default=8)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    args.raw_tsv.parent.mkdir(parents=True, exist_ok=True)
    rows = []

    def measure(phase, block, slot, binary):
        measure_path = args.output / f"{phase}-{block:02d}-{slot}.measure"
        observed = run(binary, args.input, args.output / "result.types", measure_path)
        rows.append((phase, block, slot, binary.name, *observed))

    for block in range(1, args.noise_blocks + 1):
        for slot in ("A1", "A2", "A2", "A1"):
            measure("noise", block, slot, args.A)
    for block in range(1, args.abba_blocks + 1):
        for slot, binary in (("A1", args.A), ("B1", args.B),
                             ("B2", args.B), ("A2", args.A)):
            measure("abba", block, slot, binary)

    hashes = {row[6] for row in rows}
    sizes = {row[7] for row in rows}
    if len(hashes) != 1 or len(sizes) != 1:
        raise SystemExit("A/A and ABBA outputs differ; reject the comparison")

    with args.raw_tsv.open("w", encoding="ascii", newline="\n") as raw:
        raw.write("phase\tblock\tslot\tbinary\tseconds\tpeak_rss_kib\toutput_sha256\toutput_bytes\n")
        for row in rows:
            raw.write("\t".join(map(str, row)) + "\n")
    print(f"equivalent output sha256={next(iter(hashes))} bytes={next(iter(sizes))}")
    print(f"raw observations={args.raw_tsv}")


if __name__ == "__main__":
    main()
