# PA2 implementation handoff

Stage base commit: `c9aaae664fc3ac24bee05b55421bb145b2ba1494`  
Last reviewed commit: `3a604e4c8c2f01f2df2e5cce9216f037a2139652`

## Design and completed behavior groups

PA1's tokenizer remains authoritative. The posttoken consumer handles callbacks
directly, classifies non-string tokens once, and buffers only one maximal string
run for phase 6. Numeric parsing, escape decoding, code-unit encoding, type
selection and output formatting live in `dev/src/postprocess/posttoken.cpp`.
Output is a view; no textual token stream is retained for later compiler phases.
Work is O(source bytes + output bytes); memory is O(source + pending string run
and its encoded units). Long-double bytes use the x86-64 80-bit value with zeroed
ABI padding. Buffered output flushes once at EOF.

| Owner / data flow | Complexity | Result / validation |
|---|---|---|
| Entry/simple: source → PP callbacks → simple, identifier or invalid | O(source + output); average O(1) token map | Complete; simple, invalid, keyword and operator fixtures |
| Numeric: pp-number → grammar, suffix, range/type, ABI bytes | O(token bytes); checked accumulation | Complete; integer, float, suffix and range fixtures |
| Character: spelling → escape/scalar → ABI type and bytes | O(token bytes) | Complete; escapes, Unicode, UDL and invalid-boundary fixtures |
| Strings: spellings → common encoding/suffix → code units | O(run bytes + output units); one pending maximal run | Complete; raw, numeric escape, concatenation and conflict fixtures |

Turn-start baseline was 0/26 PA2 tests passing; no coverage was removed. Final
`make test-pa2` passes 26/26, the exact prior-through check passes PA1 54/54,
and `make test-report-through-pa2` passes 80/80. The PA2 file audit passes.

## Performance evidence

Fixed A/B binaries used default `-std=gnu++11 -Wall -O3` build flags. A is
commit `d90d01a3` (per-token `std::endl`); B is commit `3a604e4c` (newline,
single EOF flush). Binary SHA-256: A `f07ce6eea9bb9c62e8743ed642268a2827d7c1b607d6bcea82c5bb23a34a06c8`,
B `58ac32156214871a49c5696282279a1c5af33c9319f2e9cd430d5fe0adcc7745`.
The 1 MiB and 8 MiB inputs repeat `item 123 1.25 "x";\n` and hash to
`c4227e211bf690504274250dc2c83a075eedac1cc74dcc9b5b30282a19064af1` and
`d8ae534f0c0f7c863f94a2d8b43de8b6b3e70000e7d5e0d3a22cc0ae2e7249ea`.
Times are wall seconds; RSS is KiB, measured with `/usr/bin/time`, stdout to
`/dev/null`.

| Workload / sequence | A time/RSS observations | B time/RSS observations |
|---|---|---|
| 1 MiB A/A calibration (3 runs) | .77/4812, .76/4816, .76/4780 | — |
| 8 MiB A/A calibration (3 runs) | 6.15/11728, 6.14/12008, 6.23/12184 | — |
| 1 MiB ABBA block 1, A1 B1 B2 A2 | .79/4848, .78/4972 | .55/4744, .38/4828 |
| 1 MiB ABBA block 2, A1 B1 B2 A2 | .78/4740, .88/4816 | .40/4844, .39/4964 |
| 8 MiB ABBA block 1, A1 B1 B2 A2 | 6.48/12200, 6.74/12184 | 3.20/11936, 3.05/12116 |
| 8 MiB ABBA block 2, A1 B1 B2 A2 | 6.96/11944, 6.78/11944 | 4.23/11912, 3.55/12196 |
| 8 MiB ABBA block 3, A1 B1 B2 A2 | 6.46/12164, 6.29/11912 | 3.16/12156, 3.30/12096 |

The 8 MiB paired-block latency reductions were 52.7%, 43.4% and 49.3%; pooled
median fell from 6.61 s (6.29–6.96) to 3.25 s (3.05–4.23). Peak RSS ranges
overlapped (A 11912–12200; B 11912–12196 KiB). The 1 MiB pooled median fell
from .785 s to .395 s. A and B produced identical 58,720,169-byte output
(SHA-256 `37b581255a37c3ada185cbd69de263c7638e98a7c059335e46b7879422698ed8`).
Compiler text size fell from 181,720 to 179,392 bytes. PA2 emits no executable,
so generated-program runtime and text-size acceptance are not applicable.

## Handoff ledger

- Implementation remaining for PA2: none; all four behavior owners above are
  complete, required coverage is unchanged, and the required checks pass.
- Independent audit: review whole-stage C++11 grammar and ABI edges, especially
  long-double padding, numeric suffixes and literal-operator token context.
  These are review questions, not waived implementation requirements.
- Boundary: hand off the PA2 implementation for Ralph's independent audit;
  this does not certify assignment advancement or waive whole-stage findings.
