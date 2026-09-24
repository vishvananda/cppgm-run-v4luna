# PA4 implementation handoff

Stage base commit: `d9e348b10c32a5c35505e5014f96abbf69a97273`
Last reviewed commit: `d9e348b10c32a5c35505e5014f96abbf69a97273`

## Design and failure groups

- **Macro replacement (71 macro fixtures; plus expression/include expansion).**
  Owner: preprocessing macro engine. Flow: phase 1–3 callbacks → located PP
  tokens → directive definitions and text-sequence rescan → phase 5–7 records.
  Required work is linear in input/output token volume, apart from bounded
  token-paste retokenization. Cover parameter collection/prescan, stringize,
  paste/placemarkers, rescanning and token-local recursion paint.
- **Directive and TU state (34 directive fixtures).** Owner: TU preprocessor.
  Flow: logical lines → conditional stack and directive dispatch → shared
  macro/include state → output token cursor. Include traversal and condition
  handling should be linear in visited source and emitted tokens. Cover
  include identity, line mapping, predefined macros, `_Pragma`, errors and
  primary-source reset.
- Turn-start failures: **105/105** were the same `EXIT_NOT_IMPLEMENTED` result
  from the PA4 scaffold; no coverage reduction is permitted. Current-stage
  acceptance is `make test-pa4`; cumulative acceptance is through PA4.

## Performance evidence

The PA4 product is preprocessing token records; executable runtime and text
size are not applicable. Record compiler wall latency and peak RSS for the
implemented tool on a fixed PA4 translation unit after correctness passes.
The full spec's A/B and executable-output protocol applies to optimization
claims; this stage makes no generated-code optimization claim. Enforce
O(source bytes + produced expansion tokens) for ordinary processing and keep
macro substitution work proportional to consumed and produced tokens.

## Handoff ledger

- **Implementation remaining:** both groups above; verify output/status,
  earlier PAs, file audit and performance evidence.
- **Independent review questions:** confirm cursor/ownership boundaries and
  recursion-paint semantics against the whole-stage audit. These questions do
  not waive the implementation requirements.
- **Handoff boundary:** only after PA4 failures reach zero (or decrease without
  losing coverage), earlier PAs pass, checks and performance evidence are
  recorded, and implementation changes are committed with a clean worktree.
