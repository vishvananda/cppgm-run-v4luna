# PA5 final architecture and performance audit

## Scope and stage boundary

- The inherited PA4 implementation checkpoint is `7fd325744fa62e76cbc45f1b1ea613e2747b6cf5`.
- The frozen PA5 comparison baseline is implementation commit
  `ecd46a9d63b4d8802132b1af7109350bb5074d2b`.
- PA5 implements the syntax subset in `pa5/README.md` and `pa5/pa5.gram`.
  It does not do type checking, overload resolution, template deduction or
  instantiation. There is no executable output at this stage.
- Course fixtures and references are unchanged. No reference correction was
  needed. New checks and benchmarks are under `student.tests/`.

## Final design and ownership trace

For `sample::Box<int> object;`, the source string is read once and passed as a
const buffer to the preprocessing cursor. The preprocessor emits located
phase-4 tokens on its worker. The parser consumes the bounded handoff and
appends tokens to the single vector owned by `Ast`; identifier spelling and
identity come from preprocessing metadata, while other spellings are interned
in the AST. The parser builds the printable declaration tree and an auxiliary
name graph in the same node arena. `sample` and `Box` are distinct name
components; `Box` owns a template-id node whose argument is a parsed type-id.
The enclosing composite spelling remains a dump view. The printer reads the
ordinary tree and preserves the checked-in output format.

`Parser` borrows the outer `Ast` owner. In `ParseTranslationUnitSource`, the
cursor is declared after the AST, so it is canceled and joined before the AST
releases metadata on both a successful return and an exception.

| Owner | Data and lifetime | Audit result |
| --- | --- | --- |
| Source reader and preprocessor | One immutable source string lives through parsing. The cursor runs the existing preprocessing pipeline on a worker and transfers at most two batches of 128 preprocessing tokens (256-token maximum handoff). Preprocessing metadata owns identifier spellings and source-file names until the returned AST is destroyed. | No push sink accumulation and no second full preprocessing/postprocessing vector. The producer drains already-published tokens before surfacing an error; destruction cancels, wakes and joins the worker. |
| Parser token pool | `Parser::tokens_` aliases `Ast::tokens`. Tokens carry spelling pointers, compact identifier IDs, category and source coordinates; they do not own a string per token. At parse completion, `compact_tokens()` retains node-referenced tokens in source order and remaps atom indexes in place. | One parser token cache, no cloned checkpoint token streams. Every retained atom index was valid in the measured AST. |
| AST and spelling owner | `Ast` owns the node and token vectors, composite dump spellings, an identifier/name interner and `PreprocessingMetadata`. It is move-only so token spelling pointers remain tied to one translation-unit owner. | Destruction is flat through vectors and containers; no recursive node ownership. AST ownership and printer implementation now live in `parser/ast.cpp` and `parser/ast_printer.cpp`; `ast_parser.cpp` is 2,972 lines, below the file-audit limit. |
| Syntax and names | Primary child/sibling links represent the stable printable tree. Auxiliary links attach qualified-name components, template-id syntax, argument lists, typed type/expression arguments and nested parser subtrees. Operator-name punctuation is retained as token leaves. Name categories use compact `NameId`s; speculative name-state changes use an undo log. | Template argument text is parsed once into typed syntax, not recovered from composite strings. Composite spellings serve the dump and are not the later semantic transport. |
| Printer | `PrintAst` reads the primary graph and renders the assignment's deterministic view. | Auxiliary structured syntax does not perturb exact output. No text is parsed back into compiler state. |

Parser checkpoints restore token position, delimiter state, AST node/composite
vector lengths and parser name facts. Name rollback cost is proportional to
mutations recorded in the active transaction. Speculation does not retain an
abandoned subtree. Predictive scans inspect the syntax region needed to decide
an ambiguity; scan counts are not instrumented, so no strict per-token work
bound is claimed from the benchmark alone.

The syntax-only qualified namespace and class-owner indexes still use rendered
strings for some owner keys. That is scoped to PA5's name-category hints; it
must not become semantic identity or a key for later type/template facts.
Stable identifier IDs are already available for leaf names.

## Findings and changes

1. **Template suffixes were flattened.** A qualified or operator template-id
   could print correctly while losing its arguments as AST syntax. The parser
   now builds name-component, template-id, argument-list and typed argument
   nodes, recursively using the type-id and expression parsers. Nested closes,
   parenthesized relational/shift expressions, packs and explicit operator
   template calls are covered. The printed dump remains unchanged.
2. **The AST was private and spelling-heavy.** `Ast`, `AstNode` and `NodeKind`
   are now public; the AST owns its spelling and preprocessing metadata.
   Identifier IDs are shared from preprocessing, other token text is borrowed
   from stable AST storage, and unneeded token slots are compacted in place.
3. **The parser/preprocessor boundary was push-only.** A bounded cursor adapter
   bridges the existing preprocessor without retaining a whole producer
   stream. Two 128-token buffers bound handoff lookahead; macro/include state
   remains owned by one translation unit.
4. **Name components and operator tokens were not fully inspectable.** Names
   now expose ordered component edges, template syntax and operator token
   leaves, including overloaded operators declared as class special members,
   with source file ID, line and column on the parsed nodes.
5. **The monolithic parser exceeded the repository source-file limit.** AST
   ownership methods and printing were moved into separately registered
   sources. This is a module split; output and parser behavior are unchanged.
6. **Error unwinding could release cursor metadata too early.** The parser now
   borrows the outer AST, whose declaration order guarantees cursor join before
   metadata destruction. The API check parses an early syntax error followed
   by a large token tail to cover cancellation.

The direct AST API check covers qualified template names, a parsed type
argument and expression subtree, explicit `operator+<int>` syntax, class
operator declarations, token and name IDs, source metadata and printing. The
separate metrics utility checks graph reachability, edges, locations and
atom/composite indexes.

## Performance and acceptance

### Frozen comparison

The final fixed input was generated with
`student.tests/benchmarks/pa5-parser-input.py --count 1500`:

- Source size: 1,004,948 bytes; SHA-256
  `49a3f6c2bd3be9cd41ce37dcc3c552ff71c6055c4559676f8345bbaeb9664ea5`.
- Generator SHA-256: `737ef2e2eb5c75b28f06bdd6989d1e30f31357215fdbf1ad48d427fe688e408b`.
- The workload contains nested and expression template arguments, declarations,
  calls, loops, memory access and floating-point expressions.
- A is commit `ecd46a9d63b4d8802132b1af7109350bb5074d2b`, binary SHA-256
  `f1b0fbf38ed2bf64cbd955617dfa4debf841f84ca9fa675b7625d586a3c2439d`.
- B is the final audited source, binary SHA-256
  `30aa699e531a6507534ecfbb2e61ff8bac75590ad2f9ecb652d15494b445389d`.
- Both used `g++ (Ubuntu 15.2.0-16ubuntu1) 15.2.0`,
  `-std=gnu++11 -Wall -O3 -pthread`, and the same dev test-runner build mode.
- `run-pa5-abba.py` ran four A/A blocks in `A1,A2,A2,A1` order, then eight
  `A1,B1,B2,A2` ABBA blocks. It used `perf_counter_ns` for wall time and
  `/usr/bin/time %M` for peak RSS. All 48 output hashes and byte counts match:
  SHA-256 `c5c5a9cfc1bc1b92ae84f741daf3e501a6991dcea3da00fb561d206375526ee4`,
  8,201,361 bytes.
- Runner SHA-256: `cb235517288fa723f4d7521be6686084978ad6a962082b067c56d13364949015`.
- Each paired ABBA delta is the mean of `B1,B2` minus the mean of `A1,A2`;
  RSS deltas use the same pairing.
- Raw observations are preserved at
  `/home/vishvananda/work/private/v4luna/artifacts/pa5-final-audit/abba-final-owned.tsv`
  (SHA-256 `33250be0f0da3d99e5963c549e79e6319d5d5511ea15859647db20e4faa29845`).
  Earlier candidate binaries and raw runs remain alongside it in that audit
  directory; this final-owned file is the accepted comparison record.

The preceding `final-split` candidate and its raw file are also retained:
binary SHA-256 `8c7deee9bea9d30c5e2ee9743d11ead681b3640558a343b3f3e528802ef45afc`,
raw SHA-256 `97e15a9c7d727e46008eb5a64967567c295ac94cca3208f7b247a648b24294f1`.
That candidate measured B−A median latency +29.0 ms (range −128.8 to +324.6
ms, B faster in 3/8 blocks) and paired RSS −913 KiB (range −264 to −1,702 KiB,
lower in 8/8). Its latency result was inconclusive. The final comparison above
uses later binaries; candidate measurements are not pooled.

The subsequent `final-operator` candidate and raw run are also retained:
binary SHA-256 `f342943e19a8ba0515257aac9e3474076af92718681fac68458915a6a0287a56`,
raw SHA-256 `7e01b9b92baeba96f670e0e0cad85c7efc041203b889a116445519ec41c66e51`.
It measured B−A latency −91.4 ms (range −179.5 to −27.6 ms, 8/8 faster) and
paired RSS −638 KiB (range −200 to −856 KiB, lower in 8/8). The final-owned
binary additionally fixes exception-path cursor/AST lifetime ordering; its
measurements below are the acceptance record.

| Metric | A | B | Paired result |
| --- | ---: | ---: | ---: |
| Compiler wall time | Median 0.819 s | Median 0.709 s | B−A median −116.5 ms; range −186.6 to +39.6 ms; B faster in 7/8 blocks |
| Peak RSS | Median 68,502 KiB | Median 68,054 KiB | B−A median −417 KiB; range −210 to −934 KiB; lower in 8/8 blocks |
| Compiler `.text` | 668,358 bytes | 677,631 bytes | +9,273 bytes (+1.39%) |
| Compiler `.data` / `.bss` | 5,360 / 1,920 bytes | 5,544 / 2,272 bytes | +184 / +352 bytes |
| Generated executable runtime / text | Not produced by PA5 | Not produced by PA5 | Not applicable at this stage |

The measured compiler-latency improvement is specific to this workload: the
paired median is 116.5 ms (about 14%) faster, with B faster in seven of eight
blocks. A/A paired absolute differences had a 3.4 ms median and a 69.6 ms
maximum; all seven faster ABBA deltas exceed that maximum, while the one slower
delta is +39.6 ms. The host remained loaded (sampled after the run at load
averages 15.64, 22.89 and 23.84), so the result does not establish a general
compiler speedup. Peak memory is lower in every block, by about 0.7% at the
medians. Compiler text grows 1.39%;
generated-program size and runtime cannot be measured in PA5.

The AST audit on this exact input reported 138,015 retained tokens and 357,035
nodes; all 357,035 nodes were reachable, with zero duplicate edges, invalid
edges, missing locations or bad token/composite indexes. It retained 9,002
composite spellings (175,512 bytes), 7,500 template-id nodes, 6,000 type
arguments and 6,000 expression arguments. These counts describe required
syntax retention; node count alone is not treated as a runtime result.

PA5 does not expose in-process phase timers or allocation counters. The frozen
runner measures whole-process wall time and peak RSS, while the AST utility
reports retained graph sizes. There is no allocator attribution in this
milestone, so this evidence does not identify per-container allocation costs.

There are no optimization passes in PA5, so transform legality,
profitability, analysis invalidation and optimization growth budgets do not
apply. The relevant pipeline bound is the 256-token producer/consumer handoff;
the AST and its referenced token pool grow with the syntax that PA5 must
preserve. No numeric cap on required AST size is imposed. The measured memory
and workload-specific latency results support the data-ownership changes;
neither establishes a compiler-wide performance claim.

### Inherited measurement and gate disposition

The prior plan's 330-byte input, SHA-256
`27d7e998057d26678100ae5243c21db56a5fde968e04a0b442e9ba4dcf50618f`, and
50-invocation observation are preserved: 0.18 s total (about 3.6
ms/invocation), 4,620 KiB peak RSS and 2,652 output bytes.
Its compiler hash was `f1b0fbf38ed2bf64cbd955617dfa4debf841f84ca9fa675b7625d586a3c2439d`.
Because the total timed interval was below a second and the source did not
exercise the full structured workload, this remains a smoke baseline, not a
performance acceptance gate. The fixed 1 MB ABBA comparison above is the
stage-scoped evidence. No latency threshold or optimization benefit is
self-imposed.

## Validation ledger

| Check | Result |
| --- | --- |
| `make -C dev cppgm++` | Pass after the final source split. |
| `make test-pa5` | Pass, 188/188. |
| Direct `student.tests/pa5-ast-api.cpp` build and run | Pass, including operator-template syntax, class operator declarations and malformed-input cancellation. |
| `student.tests/benchmarks/pa5-ast-metrics.cpp` on the frozen input | Pass; all nodes reachable and all edge/location/index counters zero. |
| `perl scripts/cppgm_file_audit.pl --stage pa5 --paths dev/src` | Pass, 32 files checked. |
| `make test-report-through-pa5` | Pass, 393/393 tests across PA1–PA5, all 5/5 stages. Run with `TEST_REPORT_ASSIGNMENT_JOBS=1 TEST_REPORT_SUBTEST_JOBS=1`; course fixture timeout and comparison rules were unchanged. |
| `git diff --check` | Pass. |

An earlier capped through-report attempt recorded 392/393 because PA3's large
`300-triple.t` hit the default 10-second text timeout while host load averaged
41.46/32.92/27.78. The final through-report rerun passed all 393 tests under
the normal timeout. The primary log provided at turn start also records
393/393. Coverage and reference comparisons were not altered.

## Handoff ledger

- **PA4 → PA5:** the prior push-style preprocessor boundary is now consumed
  through the bounded token cursor. Token spelling pointers and source-file
  IDs remain owned by the returned AST metadata. This handoff was checked with
  the full PA5 fixtures, direct API test and frozen workload.
- **PA5 → PA6 and later semantic stages:** not audited here. The next consumer
  must read the auxiliary name/template graph and compact IDs directly; it
  must not reconstruct template arguments from `composite_atoms` or the text
  dump. PA5's qualified namespace and class-owner hint maps still use strings
  and need canonical semantic identities before they own semantic lookup.
  Parser-local scope and interning indexes use standard unordered containers;
  their aggregate cost is included in RSS, but this audit has no allocator
  attribution and does not claim they meet the spec's flat-container preference
  if they become dominant in later semantic scopes.
- **Templates:** PA5 parses syntax only. There is no demanded specialization
  to trace through semantic facts, lowering, ELF or executable behavior at
  this milestone. Those audits remain for the stages that introduce them.
