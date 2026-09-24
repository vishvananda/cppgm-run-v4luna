# PA6 final architecture and performance audit

## Scope and audit basis

PA6 adds `cppgm++ --emit-types`: source is parsed by the PA5 parser, analyzed
for the assignment's scope/type subset, and rendered as a deterministic dump.
PA6 does not instantiate templates, type expressions, lower programs, emit ELF,
or generate executables. Those later-stage requirements are recorded as
handoffs below.

The audit reread `spec.md`, `pa6/README.md`, `pa6/scopes-and-types.md`,
`TESTING_AND_REFERENCES.md`, and `PROJECT_LAYOUT.md`; inspected the PA6 plan,
implementation commit `94a042c6a116241a487901650a76e30952b76c5b`, plan commit
`aba5e0d91e0344316cb9b022a336f1d75d67b4e2`, validation commit
`1b767fb4a7bf8a54b29803da43fd6d6eadb45990`, and the PA5 handoff ledger; and
traced the current parser and semantic implementation. The supplied primary
test log records 498/498 through PA6 before this audit's final changes. The
required report and file audit are rerun in the validation ledger.

No course test, oracle, comparison rule, or reference output was changed. No
reference correction was needed.

## Final design and ownership trace

For `template<class T> struct Box { T value; }; Box<int> object;`, the PA5
frontend reads and preprocesses the source, then creates one retained `Ast`.
Its ordinary child graph is the printable syntax view; its auxiliary edges
hold ordered name components, identifier IDs, and template syntax. PA6 uses
those IDs directly. It does not split a composite spelling to recover source
name components.

`AnalyzeTranslationUnit(Ast&&)` moves that AST into `SemanticUnit::Impl`. The
implementation owns the AST and one analyzer for the same lifetime; semantic
records refer to AST declarations by stable node ID. `EmitTypes` creates one
unit per input and releases it before analyzing the next input. This retains
the syntax needed by PA7 without cloning the syntax tree or serializing and
reparsing it.

| Data | Owner and identity | Lookup, lifetime, and output |
| --- | --- | --- |
| Source syntax and names | `Ast` owns tokens, nodes, source metadata, and name IDs. `NamePath` reads auxiliary component nodes; simple names use token IDs. Operator names are assembled from their operator token leaves and interned as one name. | AST text/composite spellings are used for rendering and diagnostics. The only string-to-path adapter is the public `SemanticUnit::lookup(scope, spelling)` API; source analysis uses the ID path. |
| Types | `Analyzer::types_` owns canonical `Type` records. `TypeIndex` is an open-addressed hash-to-type-ID table; collisions compare the query against the canonical record. It stores no duplicate type key or parameter vector. | Type identity is structural over kind, base/entity/declaration IDs, bound, function properties, and parameter IDs. The table grows geometrically below 75% occupancy. Template parameter identity includes its declaring AST node. |
| Scopes and bindings | Scope IDs identify lexical owners. Each scope has a small inline flat name index from name ID to the newest binding; `previous_same_name` links declarations with that name. Bindings refer to type/entity IDs. | Lookup visits the lexical chain and only explicit using-directive and inline-namespace edges. No lookup result cache needs invalidation. Output order is kept separately from lookup order for deterministic assignment output. |
| Entities | Entity IDs distinguish types, objects, functions, enumerators, namespaces, and aliases. Class/enum scopes point to their owning entity. Function entities hold normalized signatures; declaration bindings retain source parameter types. | Namespace reopening finds the existing namespace entity/scope. Declarations with the same identity extend the entity; different lexical scopes keep local classes distinct. |
| Incomplete arrays | Object declarations merge compatible array types on the object entity. At the end of analysis, one pass updates visible variable bindings from their completed object type. | The completion pass visits each scope binding once; it no longer compares every incomplete array against every binding. Aliases retain their declared type. |
| Dump | `SemanticUnit::print` renders the semantic records in recorded output order. | Text is the required tool output, not transport to a later compiler phase. PA6 produces no LowIR, MIR, ELF, or executable. |

Function source types and function identity stay separate. The source type
keeps declared parameter cv qualifiers and array/function forms for the PA6
dump and later parameter objects. The canonical signature removes top-level
cv, adjusts array/function parameters to pointers, treats a sole `void` as an
empty list, and retains member cv/ref qualifiers. Same-spelled template
parameters from different declarations have distinct type IDs.

## Findings and changes

1. **Semantic lookup was still recovering names from composite text.** The
   analyzer now consumes the PA5 auxiliary name graph and compact component
   IDs for type names, declarations, aliases, using forms, and expressions.
   The parser attaches that graph to enum and elaborated-class nodes that did
   not expose it consistently. The textual fallback was removed. Operator
   names use all operator token leaves, so `operator+` and `operator-` cannot
   collide on the shared `operator` token.
2. **Local class identity followed a rendered scope name in the inherited
   design.** Lookup and class-entity reuse now use the owning scope ID and
   name-ID index. Two `Local` classes in sibling blocks have separate scopes
   and entities. A reduced personal reproducer is
   `student.tests/pa6-local-class-scope.t`; this follows N3485 3.3.3/1 and
   9.8/1 (`doc/n3485.txt`, around lines 2702 and 12506).
3. **Function declaration matching lost type distinctions.** Canonical
   function signatures now apply parameter adjustment and top-level cv
   removal, while member cv/ref qualifiers participate in member function
   identity. Template parameter types include declaration identity. Personal
   API coverage checks equivalent `const int`/`int` and array/pointer
   declarations, distinct member qualifiers, separate same-spelled template
   parameters, and operator-name identity. The relevant C++11 rules are
   N3485 8.3.5/5–6, 9.3.1/4–5, and 13.1/2; overloaded operator names are
   described in 13.5.1–13.5.2.
4. **Array completion had cross-product work.** Completion is now a linear
   sweep of scopes and bindings. Object entities retain the merged completed
   array type, and every declaration binding is updated once at the final
   sweep.
5. **Hot semantic indexes duplicated storage.** Per-scope name lookup uses a
   flat ID map with an inline first level and linked same-name declarations.
   The type index retains only hashes and IDs; it compares collisions to the
   canonical type table and moves new records into that table.
6. **PA7 needed an owned semantic boundary.** The public move-only
   `SemanticUnit` exposes the retained AST and stable type, scope, entity, and
   binding IDs without exposing analyzer internals. `--emit-types` now uses
   this same API.

The function identity and local-scope cases are checked with personal source
and API checks as well as the course dump fixtures. Output references were
preserved; these fixes change semantic identity where the dump alone does not
show it.

## Architecture and stage-scoped acceptance

- **Identity and lookup:** Type/entity/scope/binding IDs are the semantic
  handles. Name lookup is indexed by compact name ID and follows only lexical
  parents and recorded lookup edges. Type interning hashes its structural
  operands; work is proportional to the type's parameter list on a hash or
  collision, rather than to unrelated declarations.
- **Source order and invalidation:** declarations are processed in source
  order. The class predeclaration pass exposes complete-class member types;
  node/entity maps prevent repeated class analysis. There is no semantic
  lookup cache or global generation invalidation. Array completion mutates
  the explicit object fact and then synchronizes its bindings in one final
  pass.
- **Work and growth:** parser work is inherited from the PA5 token/AST
  contract. Semantic declaration processing is bounded by AST declarations,
  name candidates, and explicit lookup edges, with a second bounded class
  type-predeclaration visit where required. Array completion is O(scopes +
  bindings). Name/type tables grow geometrically. There is no optimizer,
  worklist fixed point, IR growth, or specialization demand at this stage.
- **Optimization audit:** PA6 performs semantic normalization, not a
  semantics-preserving executable optimization. There is no transform
  legality/profitability choice, analysis invalidation, code growth, ABI
  lowering, or debug-location preservation to audit here. The canonical
  function rules above implement C++ declaration identity, not an optimization
  shortcut.
- **Storage and telemetry:** the AST and semantic records are translation-unit
  owned and released together. Type index keys are not duplicated. Some
  variable-sized type and scope lists still use `std::vector`; process peak
  RSS is measured below. PA6 does not yet expose allocation counts or
  in-process candidate/worklist counters. Specialization, cache, worklist,
  and IR counters do not exist at this stage; typed phase telemetry remains a
  cross-stage item for the next semantic/backend audits.
- **Executable measures:** PA6 emits a semantic dump only. Generated-program
  runtime and generated text size are not applicable and cannot be measured
  honestly here. The compiler executable's wall time, peak RSS, and `.text`
  size are measured. No numeric latency/RSS/code-size gate is imposed by the
  PA6 handout or this plan.

### Frozen compiler measurements

The accepted binaries use `g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0` and the
`dev/Makefile` flags `-std=gnu++11 -Wall -O3 -pthread` (no extra standard
library flags). A is the stage implementation at `94a042c6`, SHA-256
`e3d21338512310e1b5b29e3f7b56ff84f4422143bba78a8ad08c791286847012`. B is
the final audited candidate, SHA-256
`1cf2e1c2741cffc1f9583f714ae6acf119c4651d5b0f18c1d73e258c353cf213`.
The frozen input hashes are `9e7fad37d40a6aad3ca6c1368e71d3132ebbfab77f7f2563de29680e074db7b2`
for `array-8000.t` and
`154f41388ae03bbbe3f7f08258ae9bcb6cb5c96a8919fb29652a3392eaf0f4ae`
for `template-1200.t`. Their generators are fixed under
`student.tests/benchmarks/`: `pa6-array-input.py` SHA-256
`6355ec33728845077a4af79f673192f042833fc38551d59f3ad47447cc0c60f1`, and
`pa6-template-input.py` SHA-256
`8017913307237bdd32fc6b3d01df643834778979dee330576f4a98c691561426`.

`run-pa6-abba.py` uses four A/A blocks in `A1,A2,A2,A1` order and eight
`A1,B1,B2,A2` ABBA blocks. It measures wall time with `perf_counter_ns`, peak
RSS with `/usr/bin/time %M`, and hashes the complete output for every run.
Runner SHA-256 is
`95362ebe9ec13496562fd689beb02f5cc352219ce787409d3372f38cb9f50a45`.
All outputs within each workload matched across A and B.

| Workload | A median / B median | Paired B−A median and range | A/A timing spread | Peak RSS A / B median | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| 8,000 incomplete/complete array pairs, 368,000 source bytes | 1.5683 s / 0.2555 s | −1.3349 s (−84.0%); −1.5581 to −1.2422 s; B faster 8/8 | Median within-block range 10.1%, maximum 24.0% | 37,284 / 37,410 KiB; paired median +1 KiB, range −362 to +216 KiB | Large repeatable latency improvement on the affected completion workload; RSS is flat within observed variation. Output SHA-256 `dc769499591eb0cb59cc52ed40612f9474ea24d6bac0149989164990209475c7`, 640,110 bytes. |
| 1,200 template/class/function/block groups, 382,800 source bytes | 0.5847 s / 0.6166 s across the extended set | −1.3 ms (−0.3%); −63 ms to +170 ms; B faster 8/16 | Across both calibrations, median within-block range 6.4%, maximum 70.8% | 37,718 / 38,526 KiB; paired median +649 KiB, range +264 to +1,188 KiB | Latency is inconclusive under high host noise; no speedup is claimed. B uses about 1.7% more peak RSS on this workload. Output SHA-256 `a661fe5db7b7cfeddf3cb93e7c10f5eacd763a531a88d38c665519cc72cb3f83`, 968,510 bytes. |

The template result combines two independent eight-block ABBA sets and their
four-block A/A calibrations. The second calibration reached 70.8% within-block
range; paired latency deltas include both positive and negative outliers. The
median therefore does not support a latency claim. The RSS increase is
consistent across all 16 paired blocks. It is disclosed, has no PA6 numeric
acceptance cap, and does not offset the measured array-worklist improvement;
memory accounting should be revisited when PA7 extends these records.

The final B compiler has `.text` 829,470 bytes, `.data` 5,640 bytes and `.bss`
2,272 bytes. A has `.text` 824,293 bytes, `.data` 5,640 bytes and `.bss`
2,272 bytes. Compiler `.text` grows 5,177 bytes (0.63%); data and BSS are
unchanged. PA6 creates no generated executable, so generated runtime and text
growth are not applicable.

Accepted raw observations are preserved outside the checkout:

- `/home/vishvananda/work/private/v4luna/artifacts/pa6-final-audit/array-final-compact-abba.tsv`, SHA-256 `b7736a9da1c4c11bacfe4f930189d4275542eefeae43b78dd124886e65081881`.
- `/home/vishvananda/work/private/v4luna/artifacts/pa6-final-audit/template-final-compact-abba.tsv`, SHA-256 `9f6ddda78fa0883b3261e70fc5db4983d42a4caf301c316a5d411d33bc91f61d`.
- `/home/vishvananda/work/private/v4luna/artifacts/pa6-final-audit/template-confirm-abba.tsv`, SHA-256 `b29289272827460a56f64076f3b3e73b4c31417fc4542f6be734a6d1fbd7faa4`.

The first structured-name candidate's raw sets remain at
`array-structured-abba.tsv` and `template-structured-abba.tsv`; they are
superseded because the source path and type-index ownership changed afterward.
`array-final-abba.tsv` is also retained but not accepted because compilation
overlapped its measurements. Earlier `array-abba.tsv` and `template-abba.tsv`
from the implementation checkpoint are retained too. None of these
superseded observations are pooled into the final comparison.

The original 30-invocation tiny-fixture timings in the implementation plan
remain a smoke baseline: 3.3–4.3 ms per invocation and 4,872–4,968 KB peak
RSS. They are not a performance gate because startup dominated and the inputs
did not exercise the large completion or template/scope paths.

## Validation ledger

| Check | Result |
| --- | --- |
| `make build` | Pass after the final semantic/index changes. |
| `make test-pa6` | Pass, 105/105. |
| `student.tests/benchmarks/check-pa6-scope.py` | Pass: sibling local class scopes, separate template parameter types, source parameter forms, and completed array declarations. |
| `student.tests/benchmarks/check-pa6-function-identity.cpp` | Pass: equivalent normalized signatures share entities; member qualifiers, template declaration IDs, and operator names remain distinct where required. |
| `perl scripts/cppgm_file_audit.pl --stage pa6 --paths dev/src` | Pass, 34 source files checked. |
| `make test-report-through-pa6` | Pass, 498/498 tests across PA1–PA6; all 6/6 stages pass. |
| `git diff --check` | Pass before commit. |

## Handoff ledger

- **PA5 → PA6:** audited here. The semantic analyzer reads the auxiliary name
  graph and name IDs directly; it does not reconstruct qualified or template
  names from the AST dump. The named enum and elaborated-class metadata paths
  are now attached consistently. PA5's remaining strings serve parser
  name-category heuristics and presentation; they are not PA6 semantic lookup
  keys. The through-report verifies inherited AST output remains unchanged.
- **PA6 → PA7:** not audited end to end because PA7 expression and call
  semantics are not implemented in this stage. The `SemanticUnit` lifetime,
  type/scope/entity/binding IDs, source parameter types, canonical function
  signatures, and type spelling provide the intended handoff. PA7 must check
  namespace/tag hiding, the qualified scoped-enum output view, anonymous
  union member IDs, reference/array parameter facts, and complete-class
  lookups as it consumes these records. In-process allocation and candidate
  counters are also still absent and belong in the next telemetry audit.
- **PA6 → lowering/ELF:** no direct handoff is exercised yet. There are no
  selected declarations, conversions, lifetimes, layouts for code generation,
  ABI entries, LowIR nodes, machine IR, or executable benchmarks in PA6.
