# PA3 implementation handoff

Stage base commit: `18a1afab661832c2815a6b2f6eaea0d05cd5c16e`
Last reviewed commit: `18a1afab661832c2815a6b2f6eaea0d05cd5c16e`

## Design and spec alignment

`ppexpr` reuses PA1's phase 1–3 tokenizer and PA2's checked integer and
character-literal conversion. Its callback consumer converts borrowed token
spellings directly to compact facts and retains only the current logical line.
The hand-written recursive-descent parser implements PA3 precedence and
associativity, carries signed/unsigned 64-bit values, and suppresses evaluation
of unselected `&&`, `||`, and `?:` operands while still checking their syntax.
There is no PA2 text-output/reparse path or reference-tool dependency.

Input reading and token production are O(source bytes); each line's token
conversion, parse and evaluation are O(tokens). Storage is one immutable
source buffer, one reusable vector for the current line and parser recursion
for nested grammar. Generated-program runtime and generated code are not
applicable to PA3. No numeric latency/RSS gate is specified in `spec.md` or in
the inherited PA1/PA2 plans; those plans' old ratios are descriptive only.

## Behavior ledger

| Group / owner | Data flow and complexity | Validation | State |
| --- | --- | --- | --- |
| Integral and character literal facts / `posttoken` | PA1 literal callbacks → PA2 type selection, signedness and value; O(spelling bytes) | PA2 suite; PA3 integer, character, Unicode and range fixtures | Pass |
| Logical lines and `defined` / `ExpressionLine` | Phase 1–3 callbacks → compact line tokens; odd first UTF-8 byte mock; O(source bytes + tokens) | Empty/comment lines, splicing, isolation, both `defined` forms and personal reducers | Pass |
| Grammar and typed evaluation / `ExpressionParser` | Precedence levels → signed/unsigned result; O(tokens), lazy value evaluation with static type propagation for `&&`, `||`, `?:` | All PA3 operator, ordering, conditional-type, error and malformed-input fixtures | Pass |
| Output and phase errors / `ppexpr` | Each nonempty line → decimal/`u` or `error`; phase failures exit unsuccessfully; final `eof` | Full PA3 target and personal phase-error reducer | Pass |

No implementation group remains. Independent audit markers remain open: check
the whole-stage arithmetic and conversion rules against N3485 and review the
documented performance result. These are review questions, not waived work.

## Reference corrections

The checked outputs come from the assignment export commit
`8b25f727e4c6d7dca11b6acbd4a67965a62937cf`, exported from
cppgm-extended source revision `c2f713cd70d06170632bfde3e75dd6fe1aa44d98`.
The reduced cases are `-2 << 1`, `521 << 62`, and `1 << 60 << 60`.
N3485 §5.8 paragraph 2 makes a signed left shift undefined when the left
operand is negative or the shifted value cannot be represented in the
corresponding unsigned type. N3485 §16.1 paragraphs 1 and 4 require a
controlling expression to be an integral constant expression evaluated under
§5.19; §5.19 paragraph 2 excludes operations with undefined behavior from a
core constant expression, and paragraph 3 defines the integral-constant
requirement. PA3's output contract therefore requires `error` for these
expressions. The original numeric outputs wrapped or used a negative left
operand. Exactly 2,368 affected `300-triple` output rows and the third
`200-chained-shifts` row now say `error`; inputs, exit-status sidecars,
coverage and comparison rules are unchanged. The full suites pass with those
corrections.

## Performance evidence

The measured stage tool is the default release `ppexpr` build using GCC and
`-std=gnu++11 -Wall -O3`. Fixed input `pa3/tests/300-triple.t` is 12,039,435
bytes, SHA-256
`5e21eeafd74ed36dadd9dafcbd1de3dc4e7b4e3d16bb4f24f178a1c790bafdb5`.
Final binary SHA-256 is
`79afc04d051c690f14d163075b6087d464de4a6513726f86c7c5d55685879e6b`; its
`.text/.data/.bss` are `237434/2920/1744` bytes. Output is 2,090,656 bytes,
SHA-256 `739938f7f4f7aea3705b8bcd9ffbc8eb6c75b9f20b6b244231766cb99dd98fa9`.
The final tool took `4.84, 4.89, 4.95, 4.89, 4.76` seconds and used
`20408, 20412, 20012, 20100, 20356` KiB RSS. Generated executable runtime
and text size are N/A.

A character-decoder stack-storage experiment was frozen as B and compared
against final A on the same input and flags. Outputs were byte-identical.
B added 2,148 `.text` bytes (0.89%). A/A observations (wall seconds / KiB RSS)
were `10.95/20188, 8.37/20452, 8.29/20132, 10.74/20452`; four ABBA blocks
(`A1, B1, B2, A2`) were:

| Block | A1 | B1 | B2 | A2 | Paired B−A seconds |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 5.94/19932 | 4.99/20116 | 5.46/20388 | 5.60/20120 | −0.545 |
| 2 | 4.85/20388 | 4.94/20116 | 5.76/20396 | 6.41/20368 | −0.280 |
| 3 | 4.98/20080 | 5.68/20356 | 4.83/20388 | 5.40/20364 | +0.065 |
| 4 | 7.62/20084 | 8.28/20380 | 7.22/20356 | 4.94/20116 | +1.470 |

The paired latency spread crosses zero and is smaller than the observed A/A
noise; RSS ranges overlap. The experiment did not establish a repeatable
benefit, so B was reverted. No optimization benefit is claimed. The final
binary also includes later correctness fixes for `defined true/false` operands
and comparison result types in unevaluated conditional branches; the fixed
benchmark output remained byte-identical. These are stage-tool measurements,
not source-to-object compiler performance evidence.

## Handoff and validation

- No unfinished implementation group. The extension boundary for later PAs is
  the shared typed literal helper and phase/token callback path.
- Review markers are the arithmetic/conversion audit and performance evidence
  above; neither changes the implementation-complete status.
- `make test-pa3`: pass, 20/20; turn-start baseline was 0/20.
- `make test-report-through-pa2`: pass, 80/80.
- `make test-report-through-pa3`: pass, 100/100.
- `perl scripts/cppgm_file_audit.pl --stage pa3 --paths dev/src`: pass, 22
  files.
- `student.tests/pa3-expression-boundaries.input`: output diff pass.
- `student.tests/pa3-phase-error.input`: output diff pass and exit status 1.
- No required fixture input, coverage bucket, exit-status oracle or comparison
  rule was removed; only the proof-backed reference lines changed.
