# PA3 final audit

Stage base commit: `18a1afab661832c2815a6b2f6eaea0d05cd5c16e`

Implementation commit: `cb4778701473c92fdf70c655cc1b937be8b8e56d`

## Final Spec Alignment

| Surface | PA3 result |
| --- | --- |
| Input, phases 1–3 and token flow | `ppexpr` reads one immutable source string. PA1's tokenizer and cursor borrow it and emit synchronously. PA3 keeps compact typed facts for the current logical line; it does not retain spellings or use PA2's text output as transport. Tokenizer spellings are materialized temporarily for callbacks rather than represented as source ranges. |
| Literal and identifier facts | PA2's checked integer and character helpers select the PA2 type/value first. PA3 promotes signed values to its `intmax_t` model and unsigned values to its `uintmax_t` model. Identifiers need no retained identity: ordinary names become zero and `defined` retains only the mock result. |
| Parsing and evaluation | Each nonempty logical line is parsed once by the hand-written precedence parser. It evaluates directly without a syntax tree or semantic-tree copy. Logical and conditional operators parse unselected operands for grammar and type but suppress their value evaluation. |
| Canonical declarations, lookup, templates and demand | Not present in this staged expression tool; no alignment claim is made for later compiler phases. |
| Typed lowering, optimization, MIR, allocation and ELF | Not present. There is no optimization pipeline, analysis invalidation, pass-work budget or generated program in PA3. |
| Self-containment | The tokenizer, literal conversion and evaluator produce all `ppexpr` results. References are used only as checked examples; their two corrected fixtures have standard-based proof below. |

### Architecture trace

`dev/ppexpr.cpp` reads stdin in 64 KiB blocks into one owned `std::string`. `TokenizePreprocessingSource` constructs `PPTokenizer` and a `SourceCursor` that borrow that buffer. Cursor queues provide phase 1–3 lookahead; the tokenizer calls `ExpressionLine` synchronously. The callback consumer converts each preprocessing token into a compact `ExpressionToken`, retaining only the current logical line. Literal callbacks pass borrowed spelling and UCN-backslash offsets to PA2's checked helpers. `finishLine` constructs one parser over the vector, emits a decimal result (with `u` for unsigned), `error`, or no line for empty input, then clears the vector. EOF emits the final `eof` record. Phase 1–3 exceptions reach `main`, which exits unsuccessfully.

Representative expression `false ? (5 / 0u) : 1u` follows that path as follows: PA2 classifies the literals; PA3 stores their unsignedness and values; the parser validates the entire conditional expression and derives the common unsigned type; it does not execute the unselected division; the selected value is printed as `1u`. This matches N3485 §5.16 and §5.19. For `-2 << 1`, the parser produces a signed negative left operand and rejects the undefined shift under §5.8. The parser's precedence loops are linear in line tokens; recursion grows with nested parentheses, unary chains and conditionals. The reusable token vector retains capacity only up to the largest line seen. No parser checkpoints, whole-file token vector, textual token roundtrip, lookup, global retry or semantic reconstruction exists here.

The callback API passes a temporary spelling string for each token. This is not retained across callbacks, and the consumer stores only typed facts, but it does not implement the spec's preferred source-range token representation. No source-range refactor was needed for PA3 correctness or for a demonstrated performance gain; this remains a measured-design follow-up if token construction becomes a bottleneck. Character decoding also uses a callback-local `vector<DecodedAtom>` which is released after conversion, rather than a long-lived per-node allocation.

## Findings and changes

- The independent arithmetic review found no remaining PA3 defect. Literal candidate/type selection, signedness, mixed signed/unsigned conversions, left associativity, precedence, conditional result typing, logical short-circuiting, signed overflow, division edge cases and shift behavior match the handout's course definitions and N3485 §§4.5, 4.7, 4.13, 5.6–5.16 and 16.1.
- A suspected `defined and` keyword case was rejected as a false finding. PA1 emits alternative spellings such as `and` and `not` as `preprocessing-op-or-punc` (N3485 §§2.6 and 2.13); PA3's `defined` operand is specifically an identifier from PA1 preprocessing-token context. They are therefore not valid operands. The personal reducer records this rejection and confirms that `and`/`or` still work as expression operators.
- The `defined` mock uses the first UTF-8 code unit as unsigned and retains only its odd/even result. The `true` and `false` special cases apply in ordinary expressions; after `defined`, they are treated as identifier operands and use the mock, as required by the PA3 handout.
- No production source change was needed in this final audit. The new personal negative reducer and fixed benchmark generator are under `student.tests/`; default course coverage and comparisons are unchanged.

## Reference corrections

The affected references were exported in assignment commit
`8b25f727e4c6d7dca11b6acbd4a67965a62937cf` from cppgm-extended revision
`c2f713cd70d06170632bfde3e75dd6fe1aa44d98`. The reduced cases are `-2 << 1`,
`521 << 62`, and `1 << 60 << 60`:

- N3485 §5.8 p.2 makes a signed left shift undefined when its left operand is
  negative. Thus `-2 << 1` is undefined even for a shift count of one.
- `521 << 62` is undefined because `521 * 2^62` exceeds `UINT64_MAX`
  (`521 > 3`), the corresponding unsigned 64-bit limit required by the PA3
  course model.
- Shift expressions group left-to-right under §5.8 p.1. In
  `1 << 60 << 60`, the first result is `2^60`; shifting that signed positive
  value by another 60 exceeds the corresponding unsigned 64-bit range.

N3485 §16.1 p.1 requires an integral constant expression. Section 16.1 p.4
converts the remaining preprocessing tokens and evaluates them under §5.19;
§5.19 p.2 excludes operations with undefined behavior from a core constant
expression, and p.3 defines the integral-constant requirement. These cases
must produce `error` under the PA3 output contract. The former reference
numbers came from undefined signed shifts, so compiler agreement is not the
proof.

Independent comparison of the prior and current sidecars confirms that exactly
2,368 `300-triple` output rows and the third `200-chained-shifts` row changed
to `error`. Inputs, exit-status sidecars, coverage, and comparison rules are
unchanged. The full through-stage report passes with these corrections.

## Performance evidence

PA3 is an expression-evaluation tool, not a source-to-object compiler. Its
applicable measures are `ppexpr` processing latency, peak RSS and tool text
size. Generated-program runtime and generated-program text size are not
applicable. The handout sets no numeric latency/RSS threshold; the applicable
spec acceptance is linear source/token work and no claimed A/B improvement
without the frozen-binary protocol. The inherited PA1 size-doubling ratio is
descriptive, not a gate. No optimization pass exists here, so legality,
profitability, invalidation and pipeline growth budgets do not apply.

A fixed non-course workload is generated by
`student.tests/benchmarks/pa3-expression-input.py` (SHA-256
`1ac246fe70c322abac9fb8e09cc98cec5600bb21e7356c859aa46187e840084d`). It
emits 600,000 expressions (14,686,920 bytes), including varied arithmetic,
shifts, characters, alternative operators, `defined`, conditional typing and
short-circuit cases. The generated input hash is
`c5be018fde14b38f07088b419d2a7dbb078835f7899f5c7adf6f98d6158d8ef2`.
The checked output has 600,001 lines, 3,091,445 bytes and SHA-256
`202a539c235ced2db8cbdeb531808fbc6b8571974337804341e9b64a3c64d35e`; it
contains no `error` rows. The digest is a run-integrity check, not an
independent correctness oracle; course tests remain the correctness gate.

The frozen release `ppexpr` A binary and the final binary have the same SHA-256
`79afc04d051c690f14d163075b6087d464de4a6513726f86c7c5d55685879e6b`. Build
flags are `-std=gnu++11 -Wall -O3` with the repository test-runner link mode.
`size` reports `.text/.data/.bss` as `237434/2920/1744` bytes. One output-capture
run took `6.48 s / 20412 KiB`. Four identical-binary A/A runs redirected to
`/dev/null` were:

| Run | Wall seconds | Peak RSS KiB |
| ---: | ---: | ---: |
| A1 | 14.60 | 20368 |
| A2 | 13.25 | 20384 |
| A3 | 12.41 | 20352 |
| A4 | 15.07 | 19972 |

The spread is large; it supports no latency comparison or regression claim.
Peak RSS stayed near 20 MiB in these observations. No production code changed
in this audit, so there is no candidate B or A/B claim to run. Generated
program execution and generated text remain N/A.

The earlier checkpoint's character-decoder stack-storage experiment and all
its measurements are retained below as historical observations. Its record did
not preserve B's binary digest or candidate source, so it cannot satisfy the
spec's frozen A/B reproducibility rule and is not accepted as a performance
claim. Its raw data showed no repeatable benefit and the experiment was
reverted. The recorded comparison used the same input and flags and reported
byte-identical output; B added 2,148 `.text` bytes (0.89%).

| Observation | A (seconds / KiB RSS) | B (seconds / KiB RSS) |
| --- | --- | --- |
| A/A | 10.95/20188, 8.37/20452, 8.29/20132, 10.74/20452 | — |
| ABBA 1 (`A1 B1 B2 A2`) | 5.94/19932, 4.99/20116, 5.46/20388, 5.60/20120 | paired B−A −0.545 s |
| ABBA 2 (`A1 B1 B2 A2`) | 4.85/20388, 4.94/20116, 5.76/20396, 6.41/20368 | paired B−A −0.280 s |
| ABBA 3 (`A1 B1 B2 A2`) | 4.98/20080, 5.68/20356, 4.83/20388, 5.40/20364 | paired B−A +0.065 s |
| ABBA 4 (`A1 B1 B2 A2`) | 7.62/20084, 8.28/20380, 7.22/20356, 4.94/20116 | paired B−A +1.470 s |

The prior final-binary observations on course input
`pa3/tests/300-triple.t` are also preserved: input SHA-256
`5e21eeafd74ed36dadd9dafcbd1de3dc4e7b4e3d16bb4f24f178a1c790bafdb5`,
12,039,435 bytes; output SHA-256
`739938f7f4f7aea3705b8bcd9ffbc8eb6c75b9f20b6b244231766cb99dd98fa9`,
2,090,656 bytes. Recorded wall times were `4.84, 4.89, 4.95, 4.89, 4.76 s`
and RSS was `20408, 20412, 20012, 20100, 20356 KiB`. Current verification
reproduced the input, output and binary hashes. These process-time samples are
descriptive; the non-course workload above is the fixed supplemental stage
benchmark.

## Handoff ledger and validation

- **PA1/PA2 → PA3:** audited. Borrowed spelling and UCN-backslash sideband
  reach the literal helpers synchronously; ppexpr does not consume PA2 text.
  Their plan ledgers now reflect this exercised integration. PA3 ignores source
  locations because its output has no diagnostics.
- **PA3 → PA4:** still unaudited. PA4's handout requires reuse of this evaluator
  after macro expansion. The current evaluator's odd-first-byte `defined` mock
  must become an injected lookup over translation-unit macro state. The PA4
  tokenizer/macro consumer must also preserve source locations and token
  provenance across expansion; `preproc` remains a scaffold with no tokenizer
  source registered.
- Canonical C++ declarations/types/scopes, templates, demand scheduling,
  lowering, optimizers, MIR, native allocation and ELF are later-stage work and
  have no PA3 implementation surface.
- Personal reducers pass: expression boundaries; phase-error output with exit
  status 1; and `defined` alternative-token rejection plus ordinary `and`/`or`
  operators. The fixed benchmark output hash was recorded above.
- `perl scripts/cppgm_file_audit.pl --stage pa3 --paths dev/src`: pass, 22
  files.
- `make test-report-through-pa3`: pass, 100/100 tests across PA1–PA3, 3/3
  stages; no timeouts. No required fixture input, coverage bucket, exit-status
  oracle or comparison rule was removed.
