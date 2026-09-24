# PA4 final architecture and performance audit

PA4 planning/baseline commit: `37208c67310fd77133d3fc05cd2ee312c766e617`
PA4 implementation at audit start: `f8fb33634a6bfce9c389073ff5f378501a04d999`
Audit source and personal reducers: `c132236a02c88393c9b9186e3b6bfa3174445a5f`

The PA4 contract is the handout plus its 105 fixtures. No fixture, comparison,
reference output, exit-status sidecar, or coverage bucket changed. No reference
correction was needed, so the pinned bundle revision is unchanged.

## Final Spec Alignment

| Spec surface | PA4 result |
| --- | --- |
| Immutable source ownership | `preproc` reads each primary file once in fixed 64 KiB chunks into one source string. Included files are read into one string and released when recursive processing returns. Tokenization borrows those buffers synchronously. |
| Streaming phase-4 tokens | `FileTokenConsumer` processes one logical line at a time and sends expanded tokens through a sink. It does not build a translation-unit token vector. The API is push-streaming, not a pull `next()` cursor; see the PA5 handoff below. |
| Identifier identity and storage | One deque owns each distinct identifier spelling. A geometrically grown, open-addressed slot vector maps spellings to compact IDs at a 70% load threshold. Macro maps, parameters, unavailable-macro paint, and token identity use IDs. No per-identifier hash-map node or duplicate spelling is retained. |
| Source facts and provenance | Tokens carry interned ID, line/column, source-file ID, UCN backslash offsets, and macro paint. Generated tokens inherit the invocation location. Metadata is translation-unit-owned and noncopyable. |
| Direct control-expression path | `#if` replaces `defined` using the current macro-ID table, expands the expression, and passes typed PP tokens directly to PA3's evaluator. It does not print and retokenize an expression. Alternative operator tokens survive conversion. |
| Output adapter | The CLI sink writes tokens immediately through the stateful PA2 writer. That writer retains only the current phase-6 adjacent-string run and emits one `eof` per primary source. Output errors propagate to the CLI. PA2 text is an explicit tool result, not internal compiler transport. |
| Parser, semantic graph, templates, LowIR, optimization, MIR, allocation, ELF | These implementations are not present in PA4. No alignment claim is made for their later milestones. |
| Optimization legality and profitability | There is no discretionary IR optimizer in PA4. Macro replacement and recursion suppression are required language behavior, reviewed against the PA4 macro handout and its tests. Optimizer profitability, analysis invalidation, and native-code growth budgets are not applicable here. |

## Whole-stage architecture trace

`dev/preproc.cpp` reads the primary source into one immutable string, creates
one `PreprocessingMetadata`, then calls `PreprocessTranslationUnit` with a
synchronous token sink. Each call constructs fresh macro, conditional,
include-once, counter, and recursion-paint state. It clears the metadata for
that translation unit. Callers must keep the metadata alive while retaining
tokens and must discard those tokens before reusing metadata.

`TokenizePreprocessingSource` calls `FileTokenConsumer`. It interns identifier
spellings as they arrive; `PreprocessingToken` stores their compact ID and a
non-owning pointer into the stable deque. Other token spellings remain owned
where macro expansion may synthesize them. The consumer accumulates only the
current logical line plus bounded pending queues. `line.clear()` releases line
contents after processing. Included sources follow the same path while
sharing the including translation unit's IDs and macro state.

For ordinary active text, one line is appended to the pending deque and
expanded before tokenization advances to the next line. A possible function
macro invocation at a line boundary retains only the undecided invocation;
its parenthesis scanner resumes from a saved index instead of rescanning the
whole suffix on every line. Its argument tokens remain live until the closing
parenthesis, as required for substitution. `_Pragma` similarly retains only
its unresolved operand and resumes through explicit open/string/close phases.
The expanded line is passed through the pragma filter and emitted synchronously.
Thus retained token work grows with a logical line, a deferred invocation or
pragma operand, macro replacement state, unique identifier spellings, and
unique recursion contexts, rather than with all tokens in the source file.

For `#define ID(x) x`, `#define ADD(a,b) ((a)+(b))`, and
`#define TWICE(x) ID(x) ID(x)`, each `TWICE(ADD(...))` input line follows the
same ownership path: source spelling is interned once; the macro table resolves
names by ID; argument collection is bounded to that invocation; substitution
pushes compact tokens for rescanning; the recursion-paint trie canonicalizes
context; and the final expanded line is written to PA2 records. The fixed
benchmark repeats this path 40,000 times. The separate 128-name stress input
forces repeated interner growth and checks that rehashing preserves identity.

For conditionals, `replaceDefined` checks the macro table by identifier ID
before expansion. Remaining non-operator identifiers become zero. Alternative
operator spellings such as `and`, `not`, `bitand`, `or`, and `xor` remain
available to the typed PA3 evaluator. The evaluator receives PP token kinds
and spellings directly; there is no intermediate token dump or second source
tokenization. The personal reducer exercises those operators and preserves
recursion-paint examples, cross-line function-macro calls, and cross-line
`_Pragma` operands.

Macro paint is stored as IDs in an interned binary trie. Membership and path
insertion traverse the fixed 64 bits of an x86-64 `size_t`; trie nodes are
canonicalized in a geometrically grown open-addressed table. This shares
repeated contexts and prevents each expansion from copying a full list of
macro names. Table growth is geometric and the load remains below 70% after
insertions. Per-translation-unit state is released when preprocessing returns.

The CLI's `PostTokenStreamWriter` maps the emitted PP token to PA2 records and
preserves phase-6 string concatenation by holding only the current adjacent
string run. It checks each token conversion and stops further output after an
invalid token. The vector-taking PA2 adapter also returns on the first invalid
token. No posttoken vector is built by the `preproc` command.

## Findings and changes

- **Removed whole-translation-unit token ownership.** `PreprocessTranslationUnit`
  now streams to `IPreprocessedTokenSink`; the CLI connects it directly to the
  PA2 writer. Primary-file reading no longer keeps an `ostringstream` and a
  second `str()` copy. Include reading appends into one string.
- **Bounded normal-text buffering.** Ordinary lines flush incrementally.
  Function-macro and `_Pragma` lookahead has explicit resumable indices, so
  split invocations work without repeatedly scanning a growing suffix.
- **Compact, flat identifier interning.** Macro lookup, parameters, recursion
  paint, and unavailable-macro sets use IDs. The final audit replaced a
  duplicate-spelling map with one deque-owned spelling plus a flat open-address
  index. Metadata copying is disabled so token spelling pointers cannot be
  silently detached from their owner.
- **Canonical recursion contexts.** The paint trie uses compact IDs and
  canonical open-addressed nodes with geometric growth.
- **Direct typed `#if` evaluation.** The PA3 evaluator consumes the expanded
  PP tokens directly. Alternative operator identifiers are not converted to
  zero. Output writer failures are propagated.
- **Added explicit personal coverage.** `student.tests/pa4-macro-paint-trace.t`
  covers the handout's nested macro-paint patterns, alternative control
  operators, a function-like invocation split across newlines, and a split
  `_Pragma`. Its checked expected output matches. The benchmark generator is
  `student.tests/benchmarks/pa4-preprocessor-input.py`.
- No production source was added, so the existing preprocessor source-set
  registration remains sufficient. No reference files were changed.

### Behavior and ownership ledger

| Path | Retained data and growth | Validation | State |
| --- | --- | --- | --- |
| Primary/include source → tokenizer | One source string per active include depth; bounded tokenizer lookahead | All PA4 token/directive fixtures and 40k/160k workloads | Pass |
| Identifier spelling → token/macro identity | One stable spelling per unique ID plus a flat geometric slot table | All course fixtures; 128 unique macro names compared byte-for-byte with frozen A | Pass |
| Ordinary token → macro replacement | Current line, one expanded line, replacement state; deferred function arguments retained through `)` | All macro fixtures and personal paint/split-call reducer | Pass |
| `#if` source → value | One controlling-expression line and temporary typed vectors; direct evaluator call | All conditional fixtures and personal alternative-operator reducer | Pass |
| `_Pragma` source → directive effect | Only unresolved operand retained; scanner resumes from saved phase/index | Directive fixtures and personal split-operand reducer | Pass |
| PP token → PA2 record | Synchronous write; only adjacent phase-6 string run retained | All PA4 output comparisons; fixed benchmark A/B output hashes | Pass |
| Reusable parser boundary | Push sink streams without a full vector, but is not a pull cursor | API/code review | **Follow-up:** adapt to a pull cursor for PA5 |

## Performance evidence and acceptance

PA4 emits PA2 token records and does not produce an object or executable.
Generated-program runtime and generated-program text size are therefore N/A.
The applicable executable size is the `preproc` tool's text section. The
handout and PA4 row of `spec.md` set no numeric latency, RSS, or tool-text
threshold. No threshold was invented: acceptance is byte-identical output,
coverage preservation, the measured paired result, and disclosed compiler
text growth. Latency and RSS are end-to-end `preproc` wall time and GNU
`/usr/bin/time` peak RSS, including token output.

The frozen comparison uses the PA4 base executable A from commit
`f8fb33634a6bfce9c389073ff5f378501a04d999` and audit executable B from
`c132236a02c88393c9b9186e3b6bfa3174445a5f`. Both use GCC 15.2.0 and the
repository defaults `-std=gnu++11 -Wall -O3`. The executable SHA-256 values
are A `02a2c0d9ea42020adad60802b404f5b5a28b91089768a61305c0914848227454`
and B `5c7c785c68207b01f64e5741739b7951b911ae27df17ad50271e435c3617c292`.

The fixed 40k input is generated with
`python3 student.tests/benchmarks/pa4-preprocessor-input.py input.t --count 40000`.
Generator SHA-256 is
`16c0b8c992d15be5d3006900423a38fea537d7a5d8aa2ccb6b7135760c783788`;
input is 831,123 bytes with SHA-256
`8a1f62ee7359a01503e6af5710f59542ea745c479f954de7c1958533f2b23702`.
Every A/A and ABBA run produced the same 14,462,192-byte output with SHA-256
`9e785d01a4f5a23dc46ec5f71bbd9acc605072a70be08ffe8318f6ef240bc604`.

Noise calibration ran four `A1,A2,A2,A1` blocks; final comparison ran eight
`A1,B1,B2,A2` blocks. Each entry below is wall seconds / peak RSS KiB. The
raw TSVs are retained in
`/home/vishvananda/work/private/v4luna/artifacts/pa4-final-audit/`;
their SHA-256 values are `fa1f38d8259bca7326da92f59b85566c1ad1d4da33b9535e339d7efb7fd46dec`
for `noise-pa4-flat-final.tsv` and
`a8676b7b907f272f23754d55ad90ea8033b804d63ae5de412cb63082be73d99e` for
`abba-pa4-flat-final.tsv`.

| A/A block | A1 | A2 | A2 | A1 |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 2.87 / 594900 | 2.73 / 594872 | 2.72 / 594784 | 2.77 / 594692 |
| 2 | 2.75 / 594832 | 2.76 / 594844 | 2.69 / 594848 | 2.82 / 594812 |
| 3 | 2.70 / 594900 | 2.69 / 594904 | 2.73 / 594524 | 2.71 / 594904 |
| 4 | 4.85 / 594608 | 6.01 / 594820 | 5.83 / 594820 | 5.63 / 594756 |

| ABBA block | A1 | B1 | B2 | A2 |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 5.99 / 594836 | 3.32 / 4916 | 3.37 / 4864 | 5.96 / 594632 |
| 2 | 6.48 / 594916 | 3.69 / 4916 | 3.87 / 4860 | 5.93 / 594868 |
| 3 | 6.34 / 594872 | 3.57 / 4860 | 3.26 / 4856 | 6.83 / 594616 |
| 4 | 5.89 / 594728 | 3.36 / 5048 | 3.17 / 5012 | 5.52 / 594912 |
| 5 | 5.67 / 594856 | 2.77 / 4888 | 2.81 / 4912 | 5.99 / 594828 |
| 6 | 5.90 / 594848 | 3.49 / 4860 | 3.57 / 4888 | 5.98 / 594860 |
| 7 | 6.05 / 594896 | 3.39 / 4864 | 3.14 / 4916 | 3.52 / 594836 |
| 8 | 3.14 / 594848 | 1.58 / 4912 | 1.57 / 4864 | 3.14 / 594828 |

The A/A paired block-mean differences (A2 mean minus A1 mean) were
`-0.095, -0.060, +0.005, +0.680` seconds. The final A/A block and the ABBA
absolute times show host drift; its RSS remained 594,524–594,904 KiB. The
ABBA paired B-minus-A wall differences were
`-2.630, -2.425, -3.170, -2.440, -3.040, -2.410, -1.520, -1.565` seconds
(range `-3.170` to `-1.520`, median `-2.4325`). Every block favors B. The
median paired relative reduction is 43.39% (range 31.77–52.14%). Across the
16 ABBA invocations, A took 3.14–6.83 seconds and B 1.57–3.87 seconds.

Paired block-mean RSS differences (B minus A) were
`-589844, -590004, -589886, -589790, -589942, -589980, -589976, -589950`
KiB; median `-589946` KiB (about 576 MiB), with all eight blocks favoring B.
The byte-identical output and consistent RSS drop support the bounded-stream
and ownership changes; fewer retained IR nodes are not involved.

`size` reports A text/data/BSS `363793/3832/1744` bytes and B
`386154/4144/1744` bytes. The tool text grows by 22,361 bytes (6.15%), data by
312 bytes, and BSS is unchanged. This is a visible compiler-tool cost accepted
alongside the measured stage result; PA4 has no generated executable whose
runtime or code size could be traded against it.

### Larger corroborating workload

The same generator at its default count of 160,000 creates a 3,324,605-byte
input (SHA-256
`556bdd8ad7546cecd533ae07053ec0d19183c7fb4c44e0b3fe15e8c4acb3bc4f`). The
final flat-interner B and frozen A produced byte-identical 57,849,167-byte
output (SHA-256
`7f146ba711b2b84a5f249237054aef69360c33be840d8cd8544c1cd46a03308c`). Two
ABBA blocks were run:

| Block | A1 | B1 | B2 | A2 |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 11.61 / 2367496 | 6.36 / 8192 | 6.32 / 8128 | 11.45 / 2367596 |
| 2 | 11.31 / 2367676 | 6.47 / 8120 | 6.28 / 8100 | 11.06 / 2367532 |

These paired means favor B by 45.01% and 43.00%; peak RSS falls by
2,359,386 and 2,359,494 KiB. The raw ABBA TSV SHA-256 is
`292fff74e590d3644cb4abae8b26f24130cb9f4e94705904dace475859879ee1`.
An initial A-only noise sequence showed cold-to-warm drift: its seven
wall/RSS observations were `26.69/2367444, 27.00/2367048, 18.43/2367548,
11.89/2367364, 11.67/2367424, 11.86/2367516, 11.58/2367224`. That sequence
was stopped before a complete calibration; its TSV remains in the artifact
directory (SHA-256
`c3eab34669c702e478aec1561a94aac822b8f990550b5d30e5fa0661aebce838`).
Because it is only two ABBA blocks and the calibration was incomplete, this
larger input is corroboration and not a separate accepted percentage claim.

A generated 128-name macro stress input forced identifier-table growth and
repeated lookups. Frozen A and final B emitted byte-identical output (SHA-256
`957769984c68df4eff0a9cca2a24eac99cd748bb58ed4ce947a773fb125f0e7c`).

### Inherited gate review

PA1's former 1.99x wall-time ratio for a 2x input increase remains a historical
description, not an exit gate; its plan records no numeric latency or RSS
limit. PA2's flush-removal result remains supported by its own output-equivalent
ABBA evidence, while its later provenance-only fix remains explicitly
inconclusive for latency and memory. PA3 has no numeric latency/RSS threshold;
its earlier decoder experiment remains nonaccepted. No inherited numeric gate
was found that conflicts with the stage-scoped evidence, and no new arbitrary
latency, memory, or text-size threshold was added.

## Handoffs and validation

- **PA1 → PA2:** audited in the earlier plan; PA2 consumes borrowed spellings
  synchronously and retains only deferred phase-6 strings.
- **PA2 → PA3:** audited at PA3; the typed literal/evaluator helpers consume
  the shared tokenizer and do not read PA2's textual dump.
- **PA3 → PA4:** audited here. `#if` now consults the translation-unit macro
  table by interned ID; the personal reducer and conditional fixtures exercise
  alternative operators. Located tokens carry file/line/column and macro
  invocation provenance through expansion.
- **PA4 → PA5 (remaining unaudited handoff):** the current reusable API is a
  synchronous push sink. It streams and avoids a whole-file vector, but it is
  not the spec's pull token cursor. PA5 must adapt this boundary or make the
  tokenizer/preprocessor resumable before a pull-driven parser consumes it;
  metadata must remain alive while parser tokens refer to interned spellings.
- Later semantic graph, lookup, templates, demand, lowering, optimization,
  MIR, native allocation, and ELF handoffs are not present in PA4.
- No reference correction was made. There is no bundle revision to record.
- `make test-report-through-pa4`: pass, 205/205 tests across PA1–PA4; all four
  stage reports passed. Full primary log:
  `/home/vishvananda/work/.ralph/v4luna-gpt-6-luna-max/last-test.log`.
- `perl scripts/cppgm_file_audit.pl --stage pa4 --paths dev/src`: pass,
  24 files checked.
- Personal macro-paint reducer and 128-name interner comparison: pass.
- `make -C dev preproc`: pass under the repository C++11 warning flags;
  no warnings. `git diff --check`: pass.
- Required fixtures, references, output comparisons, exit-status expectations,
  and coverage were preserved.
