# Fast Compilation and Efficient Generated Code

## Purpose

This specification governs a self-contained Linux x86-64 C++11 compiler.
Correctness is mandatory. Performance has four explicit dimensions: compiler
latency, compiler peak memory, generated-program runtime, and generated code
size. Preserve fast frontend processing while making lowering and optimization
produce efficient executables. A smaller IR alone is not evidence of faster
code; runtime gains must justify added compiler work and code growth.

This run includes performance evaluation beyond the course's completion tests.
Implement required behavior in this compiler's own frontend, semantic engine,
lowering, backend and object writer. Performance must never depend on skipping
language or ABI requirements.

## Production pipeline

```text
immutable source buffers
    -> streaming preprocessor and token cursor
    -> integrated parser and semantic construction
    -> canonical typed semantic graph
    -> direct typed LowIR and bounded optimization
    -> per-function machine IR, selection and allocation
    -> direct ELF object writer
```

Textual tokens, syntax, LowIR, MIR, assembly and diagnostics are views or
explicit tool inputs/outputs. They MUST NOT transport data between production
phases. Each staged tool uses the relevant shared phase; its output requirements
MUST NOT force the object compiler to construct unused representations.

## 1. Source, preprocessing and parsing

- Retain immutable source buffers with compact file/offset identities. Tokens
  and nodes SHOULD reference source ranges instead of copying spellings.
- The preprocessor MUST expose a streaming token cursor. Retain compact tokens
  only for deferred language behavior; do not construct successive owning
  vectors of preprocessing, post, recognition and parse tokens.
- Intern identifiers as they enter the frontend. Tokens carry compact IDs or
  interned pointers, not owning strings.
- Parse each grammatical source region at most once. Retain parsed template
  bodies; instantiation substitutes and checks dependent nodes without replaying
  grammar from token positions.
- Parsing and semantic construction MUST cooperate on one typed, source-faithful
  graph. Do not build a complete syntax tree and copy it into a semantic tree.
  Preserve source locations and minimal syntax wrappers for rendering.
- Parser checkpoints MUST have bounded scope and MUST NOT clone token streams
  or retain abandoned trees.

## 2. Canonical identity and semantic facts

- Identifiers, types, declarations, scopes, template arguments, specializations,
  layouts, constants and ABI entities MUST have stable interned or compact
  identity. Hot equality and completed-fact lookup MUST be O(1) average time.
- Rendered types, names, signatures, manglings and serialized nodes MUST NOT be
  primary keys for semantic equality, lookup, substitution or invalidation.
- Published semantic nodes and environments SHOULD be immutable. Isolate
  language-required mutation in explicit fact/state records.
- Share non-dependent template nodes across instantiations. A specialization
  stores only substituted or newly established facts.
- Pack qualifiers, value categories, dependence and common location data;
  they MUST NOT create duplicate heap nodes for identical semantics.
- Record selected declarations, conversions, object identities, lifetime
  actions, layouts, ABI entries and linkage once. Later phases consume these
  facts by identity without reconstructing semantic decisions.

## 3. Lookup and overload resolution

- Index each scope by interned name and relevant declaration kind. Visit only
  lexical parents and language-required associated scopes; do not scan unrelated
  declarations or global registries.
- Store compact overload candidate sequences. Use semantics-preserving filters
  for arity, declaration kind, member/static category and template shape before
  expensive substitution and conversion work.
- Inspect every candidate required by the language. Record the selected
  declaration and conversions so lowering never repeats resolution.
- Using-directive, ADL, hidden-friend and base relationships MUST have explicit
  indexed edges, not repeated whole-program searches.
- Expected rejection returns a compact result and lazy structured diagnostics.
  Do not render strings or unwind C++ exceptions for ordinary candidate failure.
- Cache negative results only with every semantic input that affects validity.
  An incomplete key is a correctness defect.

## 4. Templates and demand

- Specialization keys include canonical template identity, arguments,
  substitution environment and every context component affecting semantics.
- Maintain separate monotonic states for declaration, definition, layout,
  defaults, exception specifications, member bodies, vtable/RTTI and emission.
  Each fact per complete key is computed once; distinguish not-started,
  in-progress, success and expected failure.
- Recursive demand observes in-progress state instead of starting duplicate
  work. Memoize success and expected failure at the narrowest correct owner,
  retaining structured information for a final diagnostic.
- Environments MUST be immutable parent-linked frames or compact overlays.
  Do not copy all visible bindings at each instantiation level.
- Transform and recheck dependent nodes only; reuse non-dependent nodes and
  facts. Demand only language-required declarations, definitions, layouts,
  initializers, support objects and bodies.
- Parsing, substitution, validation, class completion and emission are distinct
  operations. One MUST NOT invoke a broader operation merely because they share
  storage; completing a class must not instantiate unrelated member bodies.
- Represent demand with typed reasons and dependency edges. Source and output
  order are separate from semantic demand identity.

## 5. Scheduling and caches

- Propagate facts through a deduplicated worklist keyed by entity/fact identity.
  Record reverse dependencies and enqueue only consumers that can make progress.
- Adding or restoring one fact MUST NOT retry every pending class, function,
  specialization or declaration. Handle cycles with in-progress states and
  strongly connected dependencies where required.
- Caches MUST have explicit owners, complete typed keys and bounded lifetimes.
  State validity across declaration insertion, completion, substitution and
  emission. Local mutation MUST NOT clear unrelated caches or make all lookups
  cold through a global generation counter.
- Obtain deterministic output through stable IDs, source ordinals or a final
  ordering step. Do not use ordered hot containers for incidental determinism.

## 6. Typed lowering and optimization facts

- Construct typed LowIR directly from semantic facts. Text parsers and writers
  are adapters for explicit tools; production MUST NOT serialize and reparse IR.
- Constructors enforce local validity; typed references make invalid cross-links
  difficult to represent. Each function, initializer, thunk, support object and
  distinct ABI entry has one emission identity and is lowered once.
- Consume recorded declarations, conversions, layouts, lifetimes and ABI facts.
  Do not reconstruct them through names, manglings, semantic searches, cloned
  semantic trees or fake frontend nodes. Use typed lowering records.
- Preserve proven constants, ranges, alignment, object identity, alias/effect,
  escape, call-target and unwind facts when available. Keep their provenance
  and validity explicit so optimization can use them without rediscovering
  semantics. Unknown facts remain conservative; never invent stronger promises.
- Missing required semantic facts are invariant violations. Do not hide them
  with textual, name-based or whole-program recovery fallbacks.
- Full validation is appropriate for external LowIR and audit builds. Do not
  repeatedly revalidate unchanged in-memory programs between ordinary passes.

## 7. Optimization and native code

- Optimize executable work: redundant computation and memory traffic, calls,
  branches, loop work, spills/reloads and unnecessary frame or code growth.
  Use the lowering and optimization fixtures to establish legality and useful
  outcomes, without copying the reference compiler's allocation or pass order.
- Every transform MUST distinguish legality, profitability and available work
  budget. Preserve observable effects, aliasing/lifetimes, integer and floating
  semantics, ABI, exceptions and meaningful debug locations. If a proof or
  budget is unavailable, retain valid conservative IR.
- Levels select explicit policies: O0 minimizes compiler work; O1 provides
  inexpensive local simplification and propagation; O2 adds bounded dataflow,
  loop and allocation improvements; O3 permits targeted inlining, specialization,
  versioning or unrolling when expected runtime benefit justifies the cost.
  A higher level need not add a distinct pass if the output already meets its
  objectives. No level licenses unbounded search or growth.
- Start with a small set of high-value passes. Document each pass's scope,
  complexity, invalidations, work limit and code-growth limit. Bounds apply to
  the whole pipeline as well as individual transformations.
- Fixed-point transforms MUST use dirty instruction/block worklists and a
  progress measure. Do not rescan every instruction after each local change.
  Cache analyses at their natural unit and invalidate only affected facts.
- Selection and encoding MUST be linear in their input/output; the ordinary
  allocator MUST be linear or near-linear. Allocation quality must consider
  liveness, loop reuse, call clobbers and rematerialization costs, not merely
  minimize allocator runtime or static instruction count.
- Use compact per-function MIR and direct ELF emission. Do not emit/reparse
  assembly or invoke an external assembler. MIR views MUST describe the facts
  actually consumed by encoding, frame construction and unwind emission.
- Complete function-local work and release transient state promptly. Retain
  cross-function summaries and bodies only for explicit bounded interprocedural
  work or required linkage, COMDAT, initialization and relocation decisions.
  Expensive global optimization MUST NOT be required for ordinary emission.

## 8. Allocation and lifetimes

- Long-lived frontend nodes use translation-unit arenas or slabs. Parser,
  substitution, overload, lowering and backend temporaries use shorter-lived
  arenas discarded in bulk.
- Hot nodes MUST NOT use owning `shared_ptr`, individual allocation/deallocation,
  deep copies or recursive destruction. Variable children use trailing arrays,
  small inline vectors or arena slices; relationships use IDs or non-owning
  pointers with explicit lifetimes.
- Dominant maps/sets use dense or flat storage keyed by compact identity, without
  one allocation per entry. Isolate any required observable ordering from hot
  lookup. Grow large collections geometrically.
- Every source buffer, template pattern, semantic fact and IR/object buffer MUST
  have an owner and release point. Cross-phase data is the minimal typed fact
  set, not ownership of an otherwise dead phase.
- Do not retain duplicate token/syntax graphs, semantic trees, textual IR and
  function IR together. Reclaim function-local representations incrementally;
  any retained optimization bodies count against an explicit memory budget.
- Process-global mutable caches are forbidden. Read-only global tables contain
  bounded compiler metadata, not accumulated translation-unit state.

## 9. Complexity and performance evidence

- Preprocessing is proportional to source bytes and produced expansion tokens;
  parsing is proportional to tokens. Semantic work tracks actual declarations,
  lookup candidates, demanded specialization facts and dependency edges, not
  Cartesian products of unrelated collections.
- Lowering, ordinary optimization, selection, allocation and object writing
  MUST be O(n) or O(n log n) in consumed/produced IR. Broader work requires a
  documented language/ABI necessity or a capped higher-level optimization with
  measured runtime benefit. It must have a bounded conservative fallback.
- Expose low-overhead phase time, peak memory and work counters for allocations,
  candidates, specialization transitions, caches, worklists and IR sizes.
  Telemetry observes existing work, has separable overhead and does not change
  semantics or trigger extra analyses solely for reporting.
- Maintain fixed compiler and executable benchmarks covering template-heavy
  frontend work, loops, calls, memory access, floating point and self-hosting.
  Runtime inputs and checked results must prevent timing a constant-folded or
  dead workload. These benchmarks supplement assignment correctness tests.
- For performance claims, freeze A/B binaries, flags and inputs. Measure
  compilation and generated-program execution separately. Use wall-time ABBA
  blocks, A/A noise calibration and workloads long enough to dominate startup.
  Keep all observations and report paired results and spread. Hardware counters
  and Cachegrind may diagnose a result; neither is a required dependency.
- Report compiler latency/peak RSS, executable runtime and text size together.
  Check equivalent outputs before accepting results. An optimization must show
  a repeatable benefit on affected workloads and disclose regressions elsewhere;
  added compiler work, memory and code growth must stay within the level's
  documented budgets. Passing IR bounds alone does not establish runtime profit.

## 10. Self-contained implementation

- Required output MUST come from this compiler. Do not invoke an external or
  reference compiler, previous implementation or cached answer to implement
  preprocessing, semantics, lowering, code generation or object writing.
  Host linking where required by the assignments remains a separate boundary.
- Do not recognize test filenames, source snippets, library type spellings or
  expected answers. Language and library behavior follows general semantic and
  ABI rules, including for performance-sensitive cases.
- Persistent caches, precompiled headers, modules and daemon modes may be added
  later; correctness and this architecture MUST NOT depend on them.

## Architecture audit

Trace a nontrivial declaration and a demanded template from source to ELF.
Verify compact canonical keys, one parse per source region, one computation per
complete fact key, dependent-only instantiation, precise worklists and cache
invalidation, direct typed lowering, and explicit allocation/release boundaries.
Any unexplained text roundtrip, global retry, semantic reconstruction, repeated
specialization work or per-node hot allocation is a defect even when tests pass.

For optimization, trace a useful fact through lowering, a legality proof,
profitability decision, invalidation and final encoding. Check pipeline-wide
work/growth budgets, fallback behavior, ABI/debug preservation and actual spill
or loop costs. Review both compiler and executable benchmarks: faster
compilation must not hide worse generated code, and runtime gains must not hide
unbounded compiler work or disproportionate growth.
