# PA1 implementation plan

Stage base commit: `e50e87639188c5da678ab00d4d927c5d57396c65`
Last reviewed commit: `e50e87639188c5da678ab00d4d927c5d57396c65`

## Design and spec alignment

Keep stdin as an immutable UTF-8 source buffer. A bounded-lookahead cursor
performs source decoding, trigraph replacement and line splicing as tokenization
consumes input; comments and raw literals use their phase-specific paths. Emit
tokens directly through `IPPTokenStream`, preserving source locations. Expected
work is O(source bytes + emitted token bytes), with bounded lookahead except
for the token/comment currently being formed.

## Behavior ledger

| Group / owner | Data flow and complexity | Validation | State |
| --- | --- | --- | --- |
| Translation phases / `preprocess/pptoken` | UTF-8 bytes → mapped code points → trigraph and splice cursor; linear | UTF-8, trigraph, splice, BOM, EOF and trailing-backslash controls | Complete |
| Token formation / `preprocess/pptoken` | Cursor → whitespace/comments, directive headers, identifiers, pp-numbers, literals and punctuators → callbacks; linear plus output | All PA1 fixtures, including malformed inputs | Complete |

Turn-start evidence: `last-test.log` reports 0/54 PA1 tests passing, all due to
`EXIT_NOT_IMPLEMENTED`. Current `make test-pa1` and
`make test-report-through-pa1` both pass 54/54. The progress log lacked provider
baseline metadata; the full-pass branch of stageProgress is satisfied.

Reference correction evidence: the checked `pptoken` outputs came from bundle
source `c2f713cd70d06170632bfde3e75dd6fe1aa44d98` (SHA-256
`c532a109ae800825da24f60ae28ea894aa4896f56efb6ebb14728cdccf25a7d7`). The
reducer `student.tests/pa1-comment-newlines.input` contains
`a/* comment\ncontinued */b\n`. That pinned reference emits no `new-line` for
the newline inside the comment; our expected output and direct run preserve it.
N3485 §2.2 paragraph 3 requires each comment to become one space and explicitly
retains new-line characters. The reducer and the same defect in the checked
`100-extra-comments` and `900-real-world` outputs justify correcting only
those two `.ref` sidecars; inputs, status oracles and comparison rules remain
intact. N3485 §2.10 gives the C++11 pp-number grammar with signs only after
`e`/`E`, matching the `1p+3` fixture's three-token result.

N3485 §2.2 paragraph 2 appends the missing final newline before phase 2. The
personal no-final-newline input ending in `a\` confirms the appended newline is
then spliced; it produces only identifier `a`, matching the pinned reference.

## Performance evidence

PA1 emits preprocessing tokens, not executable programs; generated-program
runtime and text size do not apply. No optimization or relative speedup claim
is made. Built with `g++ -std=gnu++11 -Wall -O3`; stdout was sent to
`/dev/null`. Seven process wall-time samples (seconds): `900-real-world.t`
(4,134 bytes), `0.004406, 0.004265, 0.003986, 0.003972, 0.004949, 0.003971,
0.003911` (median `0.003986`); deterministic 4,096-line input (430,080 bytes),
`0.128147, 0.132485, 0.128210, 0.129715, 0.129907, 0.128371, 0.127907`
(median `0.128371`); 8,192-line input (860,160 bytes), `0.256970, 0.254974,
0.251946, 0.252702, 0.255313, 0.258014, 0.262856` (median `0.255313`). Each
generated line repeats `int value_17 = 0x1234 + count_17; const char* text_17 =
"token stream"; value_17 += count_17; // comment`. GNU time peak RSS was 3,584,
4,272 and 5,164 KiB respectively. Doubling generated input size took 1.99x
median time, consistent with the cursor's O(source bytes + emitted token bytes)
work. The spec sets no numeric PA1 latency/RSS budget; its linear-work rule is
the applicable acceptance bound.

## Handoff ledger

- Implementation still required: none known within PA1's handout and required
  fixtures. All 54 cases pass with inputs, status oracles and coverage retained.
- Independent review: confirm whole-stage phase/architecture details and
  location-callback compatibility with PA2–PA4 consumers. These questions are
  open for audit and do not waive implementation requirements.
- Handoff boundary: complete PA1 token formation and translation phases;
  remaining work is independent review, not an unfinished related behavior
  group.
- Validation: stage tests and through-report 54/54; `priorThroughTests` 0/0;
  file audit passed; both personal reducers and self-tokenization of the entry
  point and implementation source passed.
- Review markers: base and last-reviewed commits above are fixed for this
  implementation turn.
