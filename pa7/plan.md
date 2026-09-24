# PA7 final architecture and audit

Audit status: complete. Final implementation commit: `b8909126`.
Pre-audit checkpoint: `1407bbc5b98e5c5ddc9163037b3b778192a7afd1`.
PA7 base: `7b753b22f0f53a30e038e6775b276e9ce36ec5fc`.
Reviewed stage lineage: `b9220620` (semantic dump), `42b1657e` (member and
conversion semantics), `f36ecbf7` (type-only function templates), `27623a37`
(specialization cache identity), and `1407bbc5` (checkpoint evidence).

## Contract and final design

PA7 implements `cppgm++ --emit-semantics`. It parses each input once through
the shared preprocessing/token cursor and PA5 AST, moves that AST into the PA6
`SemanticUnit`, constructs canonical types, entities, bindings and scopes, then
walks the same source-faithful graph to analyze expressions and print the
deterministic PA7 dump. No phase transports semantic data through textual AST,
type or LowIR output. Each translation unit owns its graph and PA7 facts; the
`SemanticDumper` and its temporary caches are destroyed before the next input
translation unit is analyzed.

PA6 indexes scope declarations by interned name ID and stores semantic identity
as IDs. PA7 consumes structured name paths and indexed binding sequences rather
than reparsing rendered names. It filters the complete-TU PA6 index through a
source-order visibility state, with explicit using-directive and inline
namespace edges. Inline namespace children are indexed once when built. Small
candidate sets preserve source order; larger deduplication sets use flat ID
tables. Base traversal follows entity IDs and indexed base edges.

The dumper computes expression type, value category, binding, constant and
overload facts; applies the supported conversion rules; records a selected call
and its argument targets; then renders from the AST in source order. A single
AST pass indexes function definitions and template declarators. Deferred member
definitions and function-template instances are deduplicated by identity and
emitted in deterministic first-demand order. Call facts use an open-addressed
node-to-packed-fact table with expected O(1) lookup, avoiding one hash-map node
allocation per call. The table and its facts live only for the current
translation unit. Candidate ranking is O(C×A + V²×A) for C candidates, A
arguments and V viable candidates; arity and declaration-shape filters run
before conversion ranking.

Representative ownership traces:

- `choose(long)` declared before `run`, then `choose(int)` declared later:
  PA6 indexes both bindings, PA7 exposes only the first at `run`'s source point,
  ranks the visible candidate, records that binding and conversion, and prints
  the resolved call. The valid trace is in
  `student.tests/pa7-point-of-declaration.cpp`.
- An enum forward declaration creates the enum identity, but its scoped
  enumerator binding is not visible until its enumerator-definition. The
  `Choice::ready` use before the definition now fails; the independent reduced
  case is `student.tests/pa7-enumerator-point-of-declaration-bad.cpp`.
- For a type-only lookup hidden by a direct non-type declaration, the direct
  declaration stops lookup before a using-directive or inline namespace can
  supply a same-spelled type. The reduced case is
  `student.tests/pa7-type-only-lookup-hiding-bad.cpp`.
- A type-only function-template instance is keyed by primary function binding
  and ordered canonical type-argument IDs. Argument deduction is separated
  from type substitution; a cache hit does not repeat substitution. The
  selected synthetic binding and call conversions remain per call site. This
  is a tested PA7 extension; general body instantiation is not implemented.

## Findings and changes

- PA6 had a complete-TU declaration index, so later locals, using-directives,
  namespace names and enumerators could leak into earlier lookup. PA7 now marks
  declarations when the source walk reaches their C++ point of declaration and
  filters every relevant lookup path through that visibility state. Local
  namespace aliases are installed into the local environment; anonymous enum
  declarations bind their enumerators and emit the required simple-declaration
  record.
- Type-only lookup previously discarded a direct non-type declaration and
  continued into nominated namespaces. It now lets that direct declaration
  hide fallback candidates, after which the caller checks whether the result
  is a type.
- Scope lookup no longer scans every child scope to find inline namespaces.
  Binding-candidate, base-graph and demand membership use flat compact-ID
  structures. A direct cache comparison of the flat call-fact table against the
  former `unordered_map` showed no repeatable latency change; the flat table is
  retained for compact identity indexing and to remove per-call hash-node
  allocations. Its measured tradeoff is recorded below.
- Template specialization success identity now includes the primary binding
  and canonical ordered arguments. Context-dependent expression facts are not
  cached as context-free results. No negative specialization results are
  cached, so there is no invalidation path to become stale.
- `pa7.cpp` is 2,999 lines, within the file audit's 3,000-line limit. New
  lookup and AST-shape helpers are in `pa7_lookup.*` and `pa7_ast.*`, and both
  sources are registered for `cppgm++` in `dev/frontend_source_sets.mk`.

The point-of-declaration fixes follow C++11 N3485: §3.3.2/1 places an ordinary
declaration's point after its complete declarator and before its initializer;
§3.3.2/4 places an enumerator's point after its enumerator-definition;
§3.3.8/1 bounds a scoped enumerator's potential scope; §3.4.1/1 stops lookup
when a declaration is found; §3.4.1/6 requires namespace names to be declared
before use; §7.3.4/2 makes a using-directive effective after its appearance;
§3.3.7/1 supplies the complete-class-context rule used for member bodies.
Reduced source cases, not compiler agreement, establish the fixes. No checked
reference output required correction, and no reference bundle was revised.

## Performance evidence and acceptance

The frozen baseline is `compiler-a` from commit
`1407bbc5b98e5c5ddc9163037b3b778192a7afd1`, SHA256
`c2cf0fcbfe9d9d7876af0c858c413a063a3219cad37f34e99e2a20169f910499`. The
final candidate is the exact audited source, SHA256
`bc8f6ef4bf728fa3a7510231e7c6523ea3680fe8bbd53158972fa0fdf1b37608`.
Both were built with g++ 15.2.0, `-std=gnu++11 -Wall -O3 -pthread`, on x86-64
Intel Xeon 2.20 GHz hardware (32 logical CPUs). Each workload is one fixed
source translation unit, timed as a separate process with semantic output
directed to `/dev/null`. There are eight A/A pairs per variant, eight
baseline/current/current/baseline blocks, and three peak-RSS samples per
variant. A/A pair-delta ranges show the observed timing noise. The template
declarator run includes one A/A outlier; all eight paired blocks still favor
the candidate. Host load varied during the audit, so the paired spreads and
A/A calibration govern these claims.

| Fixed compiler corpus (bytes) | A → B median wall time (ms) | Paired B/A median (range), faster blocks | Same-binary pair-delta ranges (A; B) | Peak RSS A / B KiB, median [range] |
| --- | ---: | --- | --- | --- |
| `scope-lookup.cpp` (243,020) | 2671.4 → 229.7 | −91.10% [−91.43%, −90.80%], 8/8 | A −7.45% to +4.16%; B −1.80% to +17.92% | 39,232 [39,088–39,272] / 39,148 [39,080–39,512] |
| `template-specializations.cpp` (268,416) | 244.8 → 247.9 | +1.68% [−0.18%, +3.27%], 1/8 | A −4.76% to +5.43%; B −2.61% to +2.29% | 47,124 [47,084–47,280] / 46,940 [46,760–47,256] |
| `template-declarator-index.cpp` (592,037) | 3633.9 → 447.0 | −87.63% [−88.21%, −87.46%], 8/8 | A −5.17% to +58.28%; B −2.76% to +2.15% | 74,700 [74,400–74,840] / 74,796 [74,600–74,900] |
| `inline-namespace-lookup.cpp` (328,082) | 3090.0 → 1756.6 | −44.17% [−46.61%, −39.07%], 8/8 | A −5.08% to +0.73%; B −10.47% to +8.61% | 29,284 [29,248–29,300] / 28,600 [28,484–29,484] |

The template-specialization workload shows a small directional slowdown; its
paired range overlaps the same-binary A/A range, so it is not a stable
regression claim. The other three workloads show repeatable reductions. RSS
samples overlap and show no material stage-wide increase. The final compiler
ELF `.text` is 930,390 bytes versus 909,398 (+20,992 bytes, +2.31%); `.rodata`
is +712 bytes, `.data` is unchanged and `.bss` is +72 bytes. This is compiler
text size. PA7 produces semantic text dumps, not executables, so generated
program runtime and generated-code size are not applicable at this stage. No
numeric PA7 latency or RSS cap is specified in the assignment or `spec.md`;
no threshold was invented. The existing report-and-file-audit requirements,
correctness, coverage and this measured tradeoff are the acceptance criteria.

PA7 constructs no LowIR, MIR or executable code and has no optimization pass
pipeline. Optimizer transform legality/profitability, rewrite invalidation and
per-level pipeline work/code-growth budgets therefore do not apply here. The
relevant semantic caches are TU-scoped: expression facts are cached only when
no expected type is supplied, call facts are tied to immutable AST call IDs,
and successful type-only template instances use the complete primary-binding
and ordered-canonical-argument key. There is no negative template cache or
fixed-point retry loop. Function-definition/template-declarator indexing is one
AST pass; anonymous-type naming is a separate AST pass. Lookup and ranking work
follows the visible declarations, nominated namespace edges and actual
candidates.

The dense AST-wide call-slot experiment was rejected: its candidate peak RSS
was 40,144 KiB on scope lookup and 76,644 KiB on template declarator indexing,
versus 39,204 and 74,780 KiB for A in that run. The retained flat node-to-fact
index sizes its table according to actual call facts. In a direct eight-block
comparison against the former call-map compiler, its latency medians were
+1.18% (scope), +0.51% (template specializations), +0.74% (template
declarator) and −1.37% (inline namespace), all within paired/A/A noise.
Peak-RSS medians differed by +804,
−160, +28 and +140 KiB respectively. This refactor is not claimed as a
runtime optimization; it replaces per-call map-node allocation with bounded
flat storage, with the measured memory cost disclosed.

Raw observations and frozen artifacts are preserved outside the checkout:

- Final A/B: `/home/vishvananda/work/private/v4luna/artifacts/pa7-final-audit-flat-calls/`.
  `run-abba.py` SHA256 `eb47c0efbf743b2393cbef9b34f6e29c96d99c77292fb43cd5e1a7e5ced13965`;
  `run-abba-targeted.py` SHA256 `39064af0c35ab02357c69a5e1d31d0f423181c7dd3742710278ef9b3b439bd2c`.
  Raw timing TSV SHA256: inline `9b48b73ef93397f449cad8ff071eae4500371f1cd5144cb7a3ef1ef46ee65484`,
  scope `e9f90d020395bcab2523a6218261019b81a5f5dd4a1478bc1dd867fd6bf4c411`,
  template declarator `32e51576932e5a7aa68f5ea8275b1b477c443720bbf482547c5aa2101ec89a64`,
  template specializations `309d44a768e2419072bee2e69eb08cd0d79d5c426001cb9d2c93fafa684924f3`.
  RSS TSV SHA256: inline `025441078ec9d055f5408750fef93ad23d2d589dbfdd231a8c96bd0b65212d72`,
  scope `b95523f27358968e0d57a0d951c14a34c6ec4f65a04cccc54440b3dffc5a207d`,
  template declarator `f8090fbc2ebf7f2ac6350eaa416512263c6c690a7ce722382c0ca403021346b4`,
  template specializations `c8133c0a5a9a6c030f24234177e83f95ba62781f7ca8f252d27dccf8348f0243`.
- Direct old-map/flat-table comparison: `/home/vishvananda/work/private/v4luna/artifacts/pa7-call-cache-comparison/`;
  A (old map) SHA256 `6d284f92401c100ae1067986f03c3aa62f73bae4d7eba3634284c1991b657ada`,
  B (flat table) SHA256 `bc8f6ef4bf728fa3a7510231e7c6523ea3680fe8bbd53158972fa0fdf1b37608`.
  Script SHA256: `run-abba.py` `cd81ee24d4340971d576733e0c5bd461a0f7bca9a97d403692880d3bb465e399`,
  `run-abba-targeted.py` `ddd8e012361f983d96cca08b206abc386c235d90fd78405956f06601fc721a1f`.
  Timing TSV SHA256: inline `b3ddc76e8a881c4640673a774667c62f5b75fe9ec7f23eb9021893d5597c60be`,
  scope `8ccb550d1f0d7262ae8369cbd16ce9e3ecf544cc2004ea0ef0275721eaa9cf5b`,
  template declarator `728b6265c5997e126879c16cc618c8bee85b61137a60a3b6d4099c71f904f111`,
  template specializations `4e8229a2b9201b0185cf481385816b08a43ee8c2d2f7769b572fc057bc2c4a78`.
  RSS TSV SHA256: inline `d69375aa73416330ce2c6cad33dc07301faf31ff8f517528af9c5db3a184fc49`,
  scope `cae506ef2d2019cfb3c755467a77fd44434e26312cb1e5b4387890c330c76372`,
  template declarator `64c1d1904ab189fe6b1e71920f3e0422564b960a705ab584f64ac594b0f2604f`,
  template specializations `77831ce02cc19b01377fafa78e1460dee31fe1d17da8ff130de07ef2cc13af65`.
- The prior source-order candidate A/B run remains at
  `/home/vishvananda/work/private/v4luna/artifacts/pa7-final-audit/` (candidate
  SHA256 `6d284f92401c100ae1067986f03c3aa62f73bae4d7eba3634284c1991b657ada`;
  `run-abba.py` SHA256 `8a36466aed4fd8e3591bb755280b095e46889192e52a82ff09a24fa5c71279ec`,
  targeted runner SHA256 `680df7433df31ce1a9650838ccca291514b6522d9da051589fc87fc036634131`).
  Its four timing TSVs and four RSS TSVs are preserved there; the later flat-call
  run above is the final acceptance measurement.
- Dense-slot rejected experiment:
  `/home/vishvananda/work/private/v4luna/artifacts/pa7-final-audit-packed-calls/`
  (candidate SHA256 `38c209f67dd6c434bde95fd3d04a5689e5ac245bdaa52a585bad58bced1c0201`).
  Its four timing TSVs and four RSS TSVs are preserved there; this version used
  an AST-sized slot vector and is not the retained design.
- Final corpus SHA256: scope `a37cea6074cc7c7231c0f0d2318a90c29073d5e9731375f4931f93246aa20be8`,
  template specializations `c201442334f3fe17b7e557db8c1d3e19fcdf1ddb8f0d3e83a8247016baab8e0d`,
  template declarator `92b744b6fdc4bca1b25f0783d7cde7fc63e6196a9983b7a11fd61b5202105e42`,
  inline namespace `7e3a17f37395397e2930cc80ec91d8b1c3ca31fc10016c5a3d766e2ad22623be`.
  A/B semantic-output hashes matched respectively: `8a61aff7ee3cfa3880a208e7adece315a1cb660a7fb792b9acdeb46424ddefd1`,
  `9a1623109db5c525d4938103ac8e8539ece5e0bcc1fdf3fca56eae21ef97f60d`,
  `5b74fc074143268b9fee3373ccc2e193cd5166eb284895dcc8c2921a99a674d6`,
  `c8a995a6f30e7940ecd126c7ff72078538a5083184b9dc4f390969a9726eaad1`.

## Final validation ledger

- `perl scripts/cppgm_file_audit.pl --stage pa7 --paths dev/src`: pass, 43
  files; `semantic/pa7.cpp` is 2,999 lines.
- `make test-pa7`: 186/186 pass.
- Required `make test-report-through-pa7`: pass, 684/684 tests, all seven
  stages. The full primary log is
  `/home/vishvananda/work/.ralph/v4luna-gpt-6-luna-max/last-test.log`.
- Personal point-of-declaration valid output matches its checked expected
  output. The later-using-directive, pre-definition scoped-enumerator and
  direct-non-type-hides-imported-type reproducers each fail as required.
  Existing `student.tests/pa7-function-template-deduction.cpp` also passed
  explicitly.
- `git diff --check` and staged-diff checks passed. No course fixture,
  reference output, coverage rule or comparison rule changed.

## Final handoff ledger

No PA6→PA7 handoff remains unaudited. Structured source names, canonical type
IDs, tag/enum bindings, scope/name indexes, inline namespace edges and source
declaration order were traced into PA7 lookup. No reference correction or
bundle revision was required.

The audited future-stage boundaries are:

- PA7 `ExpressionFact`, `CallFact`, conversion and selected-call data are
  dumper-local and end with the semantic dump. PA10 source lowering must move
  required selected declarations, conversions, value categories, constants,
  object identities and lifetime actions into durable typed lowering facts;
  it must not reconstruct those decisions from dump text.
- PA7's simple derived-to-base handling walks class entity IDs but does not
  model access, ambiguous/virtual base subobjects or full class-aware
  conversion ranking. The tested PA7 subset remains supported; class/object
  model work belongs to later class stages.
- PA7's type-only function-template extension does not instantiate dependent
  bodies, non-type arguments or member templates. General template demand and
  body reuse remain with PA14–PA19.
- PA7 has no executable output, so runtime and generated-code-size coverage
  begins with lowering/backend stages. Later fixed benchmark suites must add
  loops, calls, memory, floating point and self-hosting alongside compiler
  latency and peak RSS.

The former independent-audit markers in the checkpoint below are resolved by
this audit; they are not remaining PA7 exit gates.

## Historical checkpoint record (preserved)

Stage base commit: `7b753b22f0f53a30e038e6775b276e9ce36ec5fc`
Last reviewed commit: `7b753b22f0f53a30e038e6775b276e9ce36ec5fc`
Implementation commits: `f36ecbf7`, `27623a37`

## Design and behavior groups

- **Driver and dump** — `dev/cppgm++.cpp`, `semantic/pa7.cpp`. Inputs flow
  through preprocessing, the PA5 AST and PA6 typed scopes into deterministic
  semantic output. PA7 uses AST and binding IDs directly; it does not serialize
  and reparse phase data. Work follows AST nodes and emitted output.
- **Scopes, declarations and types** — `semantic/pa6.cpp/.h`, `semantic/pa7.cpp`,
  `parser/ast_parser.cpp`. AST declarations form canonical bindings and types;
  name lookup visits enclosing scopes and nominated namespace edges. Local scope
  lookup is O(declarations in visited scopes); namespace template candidates
  use an interned-name index. Parsing is linear in tokens except bounded lookahead.
- **Expressions, conversions and calls** — `semantic/pa7.cpp`. Cached node facts
  carry type, value category, binding and constants; conversion checks feed
  overload selection and retained call targets. For C candidates, A arguments
  and V viable candidates, ranking is O(C×A + V²×A). Pointer qualification,
  reference binding, class-base conversions, conditional expressions and
  statement control checks share these facts.
- **Template function selection** — `semantic/pa7_templates.cpp/.h`,
  `semantic/pa7.cpp`, registered in `frontend_source_sets.mk`. Namespace
  template bindings are indexed once by scope/name. Explicit type arguments or
  call argument types substitute canonical type IDs through the function type;
  instances are cached by primary binding and ordered canonical template
  arguments, preserving distinct specializations when their function types
  coincide.
  Target matching or call ranking selects and emits only demanded instances.
  Work is O(type graph size) per specialization plus O(template candidates ×
  arguments × type depth) deduction. This extension covers type-only function
  templates; template bodies, non-type arguments and member templates remain
  outside PA7's required slice.
- **Anonymous storage and deferred members** — `semantic/pa6.cpp/.h`,
  `semantic/pa7.cpp`. Declaration identities feed generated storage and
  constructor actions; demanded member definitions are emitted after the main
  source walk. Work follows AST nodes and emitted members/actions.

No required PA7 behavior group remains incomplete. No course fixtures,
reference outputs or coverage rules changed. The former sole failure,
`300-static-cast-overloaded-function-template-argument.t`, now passes through
explicit type substitution, target-directed overload selection and call
argument deduction.

## Validation

- `make test-pa7`: 186/186 pass (turn-start 185/186; coverage unchanged).
- `make test-report-through-pa6`: 498/498 pass.
- `make test-report-through-pa7`: 684/684 pass.
- `perl scripts/cppgm_file_audit.pl --stage pa7 --paths dev/src`: pass, 39
  files; `semantic/pa7.cpp` is 2,998 lines (3,000-line limit).
- Personal test `student.tests/pa7-function-template-deduction.cpp` was run
  explicitly; its semantic dump matches the checked expected output. It covers
  scalar deduction, the non-template tie preference, and two distinct explicit
  template arguments whose specializations have the same function type.

## Performance evidence

PA7 emits semantic dumps, not executables; runtime and text size do not apply.
The spec sets no numeric PA7 latency/RSS budget. No optimization benefit or
speedup is claimed. Frozen A/B binaries used `g++ -std=gnu++11 -Wall -O3`:
pre-change `af6f302d` and implementation `f36ecbf7` (cache identity follow-up
`27623a37`). Inputs were
`300-reference-binding-ranking.t`,
`200-function-pointer-array-deduced-bound.t`,
`300-ranked-prefix-before-ellipsis-slot.t` (under `tests/spec/`),
`300-qualified-namespace-value-lookup.t`, and
`200-condition-declaration.t`; each was compiled once, ten times (50 TUs), and
forty times (200 TUs). Timing used wall-clock ABBA blocks and same-binary A/A
pairs; peak RSS used three `/usr/bin/time` invocations per binary and corpus.
All A/B outputs matched. SHA256: 5 TUs
`75ec0ece095a2e73ca853977113c0494c8369846d77a2289c01a340a261ada20`, 50 TUs
`0522d1e7b7054d429ae13fd91d64bddbe1c828d1c0d9645ab6d3d75a12c57d27`, 200 TUs
`b27abefe25c5bfa33864a26bf424d6e5012d424ac3076fc0f52c28901c9ff1ea`.

New A/A and ABBA observations (ms). Each ABBA block is baseline/current/current/baseline;
paired percentages compare each variant's two-run mean. Raw samples are kept
below. Medians were +1.27% (range −4.45% to +9.95%) at 5 TUs, +1.13%
(−5.73% to +8.62%) at 50 TUs and +0.57% (−2.90% to +7.85%) at 200 TUs.
A/A pair-delta ranges were −17.51% to +1.44%, −8.13% to +1.57% and −4.32%
to +4.68%, respectively; the comparisons are inconclusive.

- 5-TU A/A: `[5.906/5.535,6.399/5.368,5.966/5.403,5.438/5.517,5.460/5.281,5.672/5.437,5.945/5.331,5.825/5.706]`;
  ABBA: `[5.579/6.058/5.459/5.812,5.804/5.804/5.531/5.632,5.666/5.417/5.367/5.614,5.782/5.792/5.674/5.659,5.690/5.586/5.929/5.643,5.717/5.789/5.522/6.122,5.739/6.164/5.689/5.932,5.838/6.048/5.735/5.779,5.596/6.399/6.384/6.030,6.231/6.412/5.897/5.825]`.
- 50-TU A/A: `[22.947/22.932,24.410/23.366,23.609/22.676,24.001/24.222,24.072/24.451,25.952/24.478,24.200/22.954,24.977/23.027]`;
  ABBA: `[23.757/26.492/24.396/24.490,24.202/24.469/27.179/24.153,23.681/23.722/24.840/26.358,24.099/24.555/25.194/24.115,24.388/23.677/23.382/25.532,22.865/24.631/25.097/22.917,25.024/23.575/26.603/24.512,25.116/24.932/25.436/25.687,24.870/24.775/24.324/23.764,25.247/25.219/24.383/25.700]`.
- 200-TU A/A: `[84.854/81.265,83.776/81.267,78.347/82.102,80.206/81.296,81.240/80.375,82.032/82.060,81.536/82.564,79.272/80.398]`;
  ABBA: `[85.999/84.159/86.584/85.282,91.766/96.448/91.149/82.173,81.784/83.492/82.510/84.108,88.106/83.934/85.811/86.704,84.990/88.244/80.448/79.113,79.468/87.730/86.740/82.373,82.812/83.064/83.402/80.536,81.200/78.125/84.327/85.721,82.225/83.946/80.693/80.674,80.936/79.203/80.198/82.312]`.
- Peak RSS samples, KiB (baseline/current): 5 TUs `[4768,4656,4804] / [4832,4808,4860]`; 50 TUs `[4780,4764,4896] / [4992,4808,4720]`; 200 TUs `[5152,5044,5132] / [5384,5060,5064]`. Samples overlap.

The final cache-identity build was also frozen against `af6f302d` using the
same flags and inputs; output hashes remained identical. This batch ran under
host load averages 11.63, 9.84 and 7.25. ABBA paired medians were +2.97%
(−10.09% to +20.83%) at 5 TUs, −1.46% (−15.19% to +6.62%) at 50 TUs, and
+0.16% (−17.30% to +7.63%) at 200 TUs. A/A pair-delta ranges were −51.47% to
+36.76%, −25.72% to +24.41%, and −25.39% to +12.60%; latency is inconclusive
under this noise. Peak RSS samples, KiB (baseline/current): 5 TUs
`[4772,4912,4772] / [4776,5120,4808]`; 50 TUs
`[4712,4680,4848] / [4684,4668,4832]`; 200 TUs
`[4984,5152,4936] / [4844,4948,4932]`.

- Final cache-identity A/A (ms): 5 TUs `[18.980/14.818,20.558/12.142,12.115/8.534,11.375/8.789,7.919/8.286,9.680/7.019,9.943/6.932,8.953/12.985]`; 50 TUs `[23.960/24.091,25.569/32.277,34.132/31.767,33.012/27.689,30.497/23.548,26.420/33.765,34.935/27.430,24.461/26.311]`; 200 TUs `[114.638/118.199,98.659/92.724,109.150/84.562,85.925/93.511,115.363/116.198,94.304/106.982,96.739/99.624,91.749/93.795]`.
- Final cache-identity ABBA (ms, baseline/current/current/baseline): 5 TUs `[6.920/6.956/7.393/6.741,8.589/7.685/6.481/7.166,7.369/7.101/7.757/7.054,7.426/6.905/6.626/5.960,6.892/6.427/7.012/6.485,6.494/7.051/7.095/6.680,7.150/8.666/8.620/7.156,6.551/7.468/7.252/7.751,6.484/7.883/6.508/7.437,6.812/6.070/6.595/6.236]`; 50 TUs `[26.237/24.396/28.110/24.198,24.853/22.963/26.922/33.969,27.139/31.813/26.102/31.904,35.029/32.259/26.950/25.991,32.195/29.633/29.199/27.233,28.994/24.480/32.687/34.233,33.543/35.138/32.279/34.092,38.966/36.156/34.937/31.289,31.665/37.038/30.278/31.471,31.715/35.918/37.227/45.345]`; 200 TUs `[108.901/99.012/80.079/88.475,91.803/89.493/84.077/85.167,82.892/78.558/84.830/82.510,80.434/83.747/84.403/89.304,124.873/145.832/157.171/158.107,134.400/139.134/118.484/104.958,108.700/109.692/108.964/97.659,86.725/89.924/82.870/81.720,94.579/91.791/83.406/117.259,84.155/86.271/84.847/84.834]`.

Inherited measurements retained from earlier checkpoints: prior 30-run
single-binary median 5.757 ms (5.211–6.604), 4,668 KiB peak RSS, five-TU hash
above. A first sweep at 11.779 ms (10.837–13.079) did not reproduce. Earlier
calibrated summaries were 5-TU 5.892 vs 5.827 ms (−0.60%; range −6.34% to
+4.99%), and 50-TU 26.406 vs 25.283 ms (+4.12%) followed by 24.418 vs
24.229 ms (−0.56%; range −6.04% to +5.81%). Their raw current/prior pairs:

- 5-TU A/A `[6.286/5.890,5.536/5.638,5.721/5.648,6.274/7.349,6.143/5.706,5.722/5.926,5.679/5.806,5.696/5.715]`; ABBA `[5.808/5.914,5.990/5.765,5.486/5.857,5.675/5.607,5.941/6.038,5.978/5.694,6.036/6.135,5.539/5.797,5.843/5.720,5.964/5.939]`.
- 50-TU A/A `[25.435/25.450,25.238/25.389,25.447/24.564,24.203/23.606,23.823/24.541,24.586/24.061,26.305/24.419,24.349/25.875]`; ABBA `[23.685/25.209,24.871/23.505,24.765/24.994,24.850/23.672,23.379/24.037,24.285/24.157,23.656/23.979,25.306/25.305,24.551/25.636,24.250/24.301]`.
- Inherited peak-RSS samples, KiB: 5-TU current `[4700,4760,4692]`, prior `[4900,4780,4688]`; 50-TU current `[4604,4940,4932]`, prior `[4648,4668,4664]`.

## Historical checkpoint handoff ledger

- **Unfinished PA7 implementation:** none in the required slice. General
  template bodies, non-type template arguments and member templates remain
  future-stage work, consistent with PA7's out-of-scope boundary.
- **Independent audit:** the whole-stage review of target-directed overload
  resolution, derived-to-base cast materialization, deferred member-definition
  ordering and AST-fact lifetimes is completed in the final audit above.
- No reference corrections were needed. The final exit ledger is at the top of
  this plan.
