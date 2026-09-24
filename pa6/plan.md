# PA6 implementation plan

Stage base commit: `e15a00e9836207e440b41055ce898f6cbaa41a6e`
Last reviewed commit: `94a042c6`

## Design and completed groups

Build a translation-unit-local semantic graph over the PA5 AST. Keep AST node
identities for source declarations and use canonical type, scope, entity, and
binding identities; keep rendered output at the boundary so PA7 can extend the
graph. The PA6 handout and `scopes-and-types.md` define behavior; `spec.md`
sections 1–3 and 8–9 define the architecture and performance evidence.

| Group | Owner and data flow | Complexity target | Status |
| --- | --- | --- | --- |
| Scope graph, namespaces/classes/enums/templates, lookup and using edges | PA6 analyzer: AST declarations -> indexed scopes/entities/bindings -> dump | O(AST nodes + lookup candidates + edges); average O(1) indexed lookup | Complete; namespace, inline namespace, alias, and shadowing tests pass |
| Canonical types, declarators, aliases, matching, parameter adjustment, arrays | Type builder: recursive declarator AST -> canonical IDs -> declaration facts | O(declarator nodes + type operands), average O(1) interning | Complete; declarator, array, reference, and function tests pass |
| Integral constants, bounds, enumerators, `sizeof`/`alignof`, `decltype`, assertions | Evaluator over existing AST and semantic facts | O(expression nodes evaluated); short-circuit unselected operands | Complete; constant and assertion tests pass |
| Dump integration and translation-unit isolation | `cppgm++ --emit-types` invokes the analyzer once per input | O(output size) after analysis | Complete; full PA6 suite passes |

## Performance evidence

Measured 2026-09-24 with 30 sequential fresh invocations per input using
`dev/cppgm++ --emit-types -o /dev/null <input>` under GNU `/usr/bin/time`.

| Input | Bytes | Total wall time / 30 | Mean per invocation | Peak RSS |
| --- | ---: | ---: | ---: | ---: |
| `general/100-class-forward.t` | 16 | 0.10 s | 3.3 ms | 4,872 KB |
| `general/200-inline-namespace-alias-lookup.t` | 199 | 0.11 s | 3.7 ms | 4,968 KB |
| `general/300-declaration-forms-valid.t` (largest valid fixture) | 1,749 | 0.13 s | 4.3 ms | 4,936 KB |

PA6 emits a semantic dump, not an executable, so runtime and text size are not
applicable. No optimization or performance improvement is claimed. PA6 has no
numeric latency/RSS cap; acceptance is indexed, source-graph-sized work and
storage. Executable runtime and code-growth gates belong to later stages that
emit programs.

## Handoff ledger

- Remaining required implementation groups: none identified against the PA6
  contract. Coverage is unchanged; no reference files were modified.
- Validation: `make test-pa6` 105/105; `make test-report-through-pa6`
  498/498; `perl scripts/cppgm_file_audit.pl --stage pa6 --paths dev/src`
  passed (34 files).
- Independent audit questions: review namespace/tag hiding interactions and
  the qualified enum output view as PA7 consumes these identities; confirm the
  anonymous-union token extent remains separate from source locations. These
  are review questions, not waived requirements.
- Implementation commit: `94a042c6` (`Implement PA6 semantic scopes and types`).
  Handoff requires this plan update committed and a clean worktree; PA6 passes,
  subject to the stage audit.
