# PA4 implementation handoff

Stage base commit: `d9e348b10c32a5c35505e5014f96abbf69a97273`
Last reviewed commit: `d9e348b10c32a5c35505e5014f96abbf69a97273`

## Design and coverage

- **Macro replacement (71 macro fixtures plus expansion in directives).** Owner:
  `Preprocessor::expand`, `substitute`, and `applyPastes`. Flow: phase 1–3
  callbacks → located preprocessing tokens → argument prescan/stringize/paste
  → token rescan → PA2 token consumer. Source paths are interned per
  translation unit; macro output inherits the invocation location. Substitution
  scans consumed and emitted tokens; paste reduction is one pass plus
  retokenization of pasted spellings. Recursion contexts use a persistent
  fixed-depth trie (64 steps on x86-64).
- **Directives and translation-unit state (34 directive fixtures).** Owner:
  `processFile` and directive handlers. Flow: logical lines → conditional stack
  and directive dispatch → shared macro/include state → expanded token stream.
  Include traversal and ordinary line handling are linear in visited source and
  tokens. Each primary source gets fresh macro, counter, conditional and
  pragma-once state.
- Turn-start coverage was 105/105 `EXIT_NOT_IMPLEMENTED`. All 105 fixtures
  remain enabled; no tests or references were changed.

## Validation and performance

- `make test-pa4`: 105/105 passed.
- Required prior-stage report through PA3: 100/100 passed.
- `perl scripts/cppgm_file_audit.pl --stage pa4 --paths dev/src`: 24 files
  passed.
- Final preprocessor, 10 serial runs on
  `pa4/tests/directives/600-repeated-argument-expansion.t` (190,206 source
  bytes): 0.16–0.17 s wall time, median 0.16 s; peak RSS 36,388–36,608 KiB.
  PA4 emits token records (1,076 bytes for this input), not an executable, so
  runtime and executable text size do not apply. No optimization benefit is
  claimed; the measurements are stage evidence, not an A/B comparison.

## Handoff ledger

- **Implementation remaining:** none in the PA4 contract. Required stage,
  prior-stage and file-audit checks pass with unchanged coverage.
- **Independent audit questions:** review the course-specific recursion paint
  boundaries and ownership across the phase-4 token handoff. These are audit
  questions, not waived implementation requirements.
- **Handoff:** implementation is ready for Ralph's full-stage audit. This does
  not certify later assignments.
