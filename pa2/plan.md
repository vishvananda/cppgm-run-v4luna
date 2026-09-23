# PA2 implementation plan

Stage base commit: `c9aaae664fc3ac24bee05b55421bb145b2ba1494`  
Last reviewed commit: `c9aaae664fc3ac24bee05b55421bb145b2ba1494`

## Design and acceptance

Keep the PA1 tokenizer authoritative and consume its callbacks directly. Classify
each non-string preprocessing token once; buffer only a maximal adjacent string
run, because phase 6 needs its combined encoding, suffix and code units. Keep
literal parsing and encoding in reusable `dev/src` code; output is a view, not
state transported to later compiler phases. Target work O(source bytes + token
bytes), plus output, and memory O(largest string run + largest token). PA2 emits
no executable, so runtime/text-size acceptance does not apply. Record posttoken
latency and peak RSS on fixed small and large inputs after correctness passes.

## Groups, complexity and validation

| Owner / data flow | Work and bound | Validation |
|---|---|---|
| Entry and simple tokens: PP callbacks → simple/identifier/invalid | wire stream; keyword/operator lookup average O(1) | simple, reserved suffix, invalid-token fixtures |
| Numeric literals: pp-number → grammar, type/range, ABI bytes | linear parse/checked accumulation per token | integer, suffix, float, range fixtures |
| Character literals: spelling → decoded scalar → ABI type/bytes | linear escape/UTF decoding per token | ASCII, escapes, Unicode, invalid boundaries |
| String runs: spellings → code units → concatenated output | linear in run bytes and produced units; one pending run | raw, numeric escapes, suffix/prefix conflicts, hard concat fixtures |

Turn-start evidence: 0/26 PA2 fixtures pass (current `last-test.log`); PA1
through-report passes. No PA2 handoff ledger existed.

## Handoff ledger

- Implementation remaining: all four groups above until required PA2 fixtures
  pass; update this ledger with completed owners, commits, and validation.
- Independent audit questions: verify contract edges and performance evidence;
  these do not waive implementation requirements or certify PA2 as complete.
- Boundary: only hand off after correctness and coverage hold, file audit and
  earlier PAs pass, performance evidence is recorded, changes are committed,
  and the worktree is clean.
