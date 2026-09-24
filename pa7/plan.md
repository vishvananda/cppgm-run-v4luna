# PA7 implementation handoff

Stage base commit: `7b753b22f0f53a30e038e6775b276e9ce36ec5fc`
Last reviewed commit: `7b753b22f0f53a30e038e6775b276e9ce36ec5fc`

## Design and behavior groups

PA7 keeps the PA5 AST and PA6 canonical declaration, type, and scope graph.
Typed expression facts are cached by AST node; calls retain selected bindings
and argument conversion targets; output walks syntax in source order. No source,
AST, or semantic-data text round trips are used.

- **Driver and dump** — owner: `dev/cppgm++.cpp`, `dev/src/semantic/pa7.cpp`.
  Data flow: arguments → preprocessed translation units → PA6 semantic units →
  deterministic PA7 dump. Work is O(AST nodes + emitted output).
- **Types, expressions, conversions, and calls** — owner:
  `dev/src/semantic/pa7.cpp` using typed constructors in PA6. Data flow: AST node
  → cached type/category/constant facts → viable conversion sequences and call
  candidates → resolved output. Work is O(expression nodes + the candidates
  examined for each call × its arguments).
- **Declarations and scope lookup** — owner: `dev/src/semantic/pa6.cpp/.h` and
  `pa7.cpp`. Data flow: AST declaration/statement → canonical binding and scope
  IDs → local, enclosing, and nominated-name lookup. Work follows declarations,
  traversed scopes, using-directive edges, and relevant candidates; no global
  declaration-pair scan is introduced. Includes condition declarations,
  unbraced substatement scopes, namespace aliases, and local anonymous unions.
- **Parser boundary** — owner: `dev/src/parser/ast_parser.cpp/.h`,
  `ast_functional_type.cpp`, and `ast.cpp`. Data flow: token stream →
  transactional declaration probe / expression parse → AST with source spans.
  Parsing is linear in tokens apart from bounded lookahead. Covers declaration
  versus expression disambiguation, block namespace aliases, `nullptr_t`, and
  multiword fundamental functional casts.

## Validation and performance evidence

- `make test-pa7`: 171/186 pass (0/186 at turn start); no fixture coverage was
  removed. The two local anonymous-union cases were also checked against their
  full expected dumps.
- Required through-PA6 report: 498/498 pass.
- PA7 file audit: 37 files pass; `ast_parser.cpp` is within its 3,000-line
  limit.
- Compiler baseline: 30 invocations of `dev/cppgm++ --emit-semantics` over
  `300-reference-binding-ranking.t`,
  `200-function-pointer-array-deduced-bound.t`,
  `300-ranked-prefix-before-ellipsis-slot.t`,
  `300-qualified-namespace-value-lookup.t`, and
  `200-condition-declaration.t`. Median 5.757 ms (min 5.211, max 6.604); a
  same-command `/usr/bin/time -v` run measured 4,668 KiB peak RSS. Output SHA256:
  `75ec0ece095a2e73ca853977113c0494c8369846d77a2289c01a340a261ada20`.
  PA7 emits a semantic dump, so executable runtime and text size do not apply.
  No optimization benefit is claimed and no stage-specific performance gate is
  added.

## Handoff ledger

- Required PA7 procedural, non-template behavior is implemented. The remaining
  15 failures exercise behavior outside the PA7 boundary: anonymous class and
  namespace-scope anonymous-union identity; nested class/member access and
  constructors; derived-to-base conversions; member pointers and member
  function overloads; deferred/template calls; or reference binding beyond the
  listed basic cases. Their exact fixtures remain unchanged:
  `100-distinct-anonymous-type-identities`,
  `100-namespace-static-anonymous-union-injected-members`,
  `100-private-nested-out-of-class-constructor-definition`,
  `100-private-nested-out-of-class-member-definition`,
  `100-qualified-base-data-member-hides-function`,
  `200-derived-to-base-pointer-prefers-less-qualified-overload`,
  `300-deferred-demand-closure`, `300-elaborated-local-struct-copy-init`,
  `300-member-cv-overload-identity`,
  `300-member-function-pointer-return-pointer-const`,
  `300-member-function-pointer-type-alias-and-function`,
  `300-member-pointer-type-alias-and-function`,
  `300-reference-binding-pointee-const-pointer`,
  `300-static-cast-member-overload-prefers-nontemplate`, and
  `300-static-cast-overloaded-function-template-argument`.
- Independent audit questions: verify the later PA scopes and class-aware
  resolution can extend the AST-node binding/fact maps without reparsing; review
  candidate ranking and synthetic anonymous-entity identity during the full
  stage audit. No known defect remains in the PA7 required slice.
- No tests or references were changed. Implementation commit:
  `b9220620e3fd0ff50f34ca74e78da4200078dac2`.
