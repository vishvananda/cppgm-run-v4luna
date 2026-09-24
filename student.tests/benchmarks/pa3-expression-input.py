#!/usr/bin/env python3

"""Emit a fixed, varied ppexpr workload for stage latency measurements."""

COUNT = 50000

for value in range(COUNT):
    other = value % 97
    rows = (
        f"({value} + 13) * 7 - 5",
        f"{value}u << {value % 63}u",
        f"({value} & 31) < 17 ? {value}u : {value}",
        f"({value} % 97) == {other}",
        f"({value} < 25000) and ({value} != 0)",
        f"defined a{value}",
        f"false ? ({value} / 0) : ({value} >> 2)",
        f"('A' + {value}u) & 0xffu",
        f"~{value} & 255",
        f"({value} * 3 + 7) / 2",
        f"({value} | 0x40) ^ ({value} & 0x3f)",
        f"({value} >= 10 ? 1 : 0) || ({value} == 0)",
    )
    for expression in rows:
        print(expression)
