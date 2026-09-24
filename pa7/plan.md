# PA7 implementation handoff

Stage base commit: `7b753b22f0f53a30e038e6775b276e9ce36ec5fc`
Last reviewed commit: `7b753b22f0f53a30e038e6775b276e9ce36ec5fc`

## Design and behavior groups

PA7 uses the PA5 AST and PA6 canonical declaration, type, and scope graph.
Expression facts flow from AST nodes through conversion/candidate selection to
the deterministic semantic dump; no text round trips or fixture-specific logic.

- **Class types and lookup** — owner: `dev/src/semantic/pa6.cpp/.h`,
  `pa7.cpp`. Class declarations add base-entity edges and member-pointer types;
  expression lookup follows direct declarations and base edges. Work is
  O(declarations + traversed base edges + relevant lookup candidates).
- **Conversions and member functions** — owner: `dev/src/semantic/pa7.cpp`.
  Operand facts and target types feed conversion ranking, overload selection,
  cast nodes, and deferred member-definition output. Work is O(expression nodes
  + candidates examined × arguments), with visited sets for base traversal.
- **Anonymous storage and construction actions** — owner:
  `dev/src/semantic/pa7.cpp` with identities from PA6. AST declarations map to
  synthetic local class names or anonymous-union storage and constructor
  actions. Work is O(AST nodes + emitted members/actions).
- **Defaulted nested constructors** — owner: `dev/src/parser/ast_parser.cpp`
  and `pa6.cpp`. Tokens become a function-definition AST; qualified binding
  resolves through nested class scopes. Parsing is linear in tokens plus the
  qualified lookup path. In-class `= default` remains a declaration.

## Validation and performance evidence

- `make test-pa7`: 185/186 pass, improving from the turn-start 171/186 with
  all 186 fixtures retained. The sole failure is
  `300-static-cast-overloaded-function-template-argument.t`.
- `make test-report-through-pa6`: 498/498 pass.
- `perl scripts/cppgm_file_audit.pl --stage pa7 --paths dev/src`: pass, 37
  files; `ast_parser.cpp` is exactly at its 3,000-line limit.
- Performance corpus: same five fixtures and flags as the inherited plan.
  Thirty-run prior single-binary record was median 5.757 ms (5.211–6.604),
  peak RSS 4,668 KiB, output SHA256
  `75ec0ece095a2e73ca853977113c0494c8369846d77a2289c01a340a261ada20`.
  Repeated 10-block ABBA medians were 5.892 vs 5.827 ms current/prior
  (paired −0.60%, range −6.34% to +4.99%); A/A calibration showed similar
  run-to-run variation. The 50-TU corpus (same five files repeated ten times)
  had one ABBA run at 26.406 vs 25.283 ms (+4.12%), then 24.418 vs 24.229 ms
  (−0.56%, range −6.04% to +5.81%). The second run's A/A pairs varied by up
  to 7.17%, so the latency comparison is inconclusive. Outputs matched across
  binaries: five-TU SHA256 above; 50-TU SHA256
  `0522d1e7b7054d429ae13fd91d64bddbe1c828d1c0d9645ab6d3d75a12c57d27`.
  Peak RSS observations, KiB: five-TU current `[4700,4760,4692]`, prior
  `[4900,4780,4688]`; 50-TU current `[4604,4940,4932]`, prior
  `[4648,4668,4664]`. First single-binary sweep measured 11.779 ms
  (10.837–13.079), not reproduced by calibrated runs. PA7 emits no executable,
  so runtime/text size do not apply. The spec sets no numeric PA7 budget; no
  optimization benefit or performance pass/fail gate is claimed.
  Raw latest calibration and ABBA pairs (ms, current/prior where paired):
  five-TU A/A `[6.286/5.890,5.536/5.638,5.721/5.648,6.274/7.349,6.143/5.706,
  5.722/5.926,5.679/5.806,5.696/5.715]`; ABBA
  `[5.808/5.914,5.990/5.765,5.486/5.857,5.675/5.607,5.941/6.038,
  5.978/5.694,6.036/6.135,5.539/5.797,5.843/5.720,5.964/5.939]`.
  50-TU A/A `[25.435/25.450,25.238/25.389,25.447/24.564,24.203/23.606,
  23.823/24.541,24.586/24.061,26.305/24.419,24.349/25.875]`; ABBA
  `[23.685/25.209,24.871/23.505,24.765/24.994,24.850/23.672,23.379/24.037,
  24.285/24.157,23.656/23.979,25.306/25.305,24.551/25.636,24.250/24.301]`.

## Handoff ledger

- **Unfinished implementation:** the sole failing fixture needs function-template
  argument selection and template deduction/instantiation (`take` and
  `hello<stream>`). PA7 README explicitly places template functions outside its
  required slice. This remains a later-stage implementation item; the fixture
  and coverage are unchanged.
- **Independent audit:** the whole-stage audit should review target-directed
  overload resolution, derived-to-base cast materialization, and deferred
  member-definition ordering. No defect in these paths is known from the
  passing fixtures; this note does not waive audit findings or requirements.
- No tests or references were changed. Implementation commit: `42b1657e`.
