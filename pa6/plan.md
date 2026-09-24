# PA6 implementation plan

Stage base commit: `e15a00e9836207e440b41055ce898f6cbaa41a6e`
Last reviewed commit: `e15a00e9836207e440b41055ce898f6cbaa41a6e`

## Design and spec alignment

Build a translation-unit-local semantic graph from the structured PA5 AST;
retain AST node identities for source declarations, and use canonical type,
scope, entity, and binding identities rather than rendered strings. Extend the
shared graph so PA7 can consume it. Keep deterministic rendering at the edge.
Required behavior and boundaries are defined by `README.md` and
`scopes-and-types.md`; production data flow and compact identity follow
`../spec.md` sections 1–3 and 8–9.

## Behavior groups and evidence

| Group | Owner and data flow | Complexity target | Validation |
| --- | --- | --- | --- |
| Scope/declaration graph, namespaces/classes/enums/templates, lookup and using edges | PA6 analyzer: AST declarations -> indexed scopes/entities/bindings -> dump | O(AST nodes + lookup candidates + edges) | `100-*`, class/namespace/using spec and general fixtures |
| Canonical types, declarators, aliases, matching, parameter adjustment and array completion | Type builder owned by analyzer; recursive AST declarators -> canonical IDs -> declaration facts | O(declarator nodes + type operands), average O(1) intern/lookups | `200-*` type/declarator/array fixtures |
| Integral constants, bounds, enumerators, `sizeof`/`alignof`, `decltype`, assertions | Constant/type-expression evaluator over existing AST and type facts | O(expression nodes evaluated), short circuit unselected operands | focused `200-*` and `300-*` cases |
| Dump integration and translation-unit isolation | `cppgm++ --emit-types` driver invokes analyzer once per input and writes ordered units | O(output size) | full `make test-pa6`, multi-file fixtures |

Performance evidence: measure PA6 compiler wall time and peak RSS on a small,
medium, and largest checked-in valid input after the mode works; retain the
commands and results here. PA6 emits no executable, so runtime and text-size
evidence are not applicable. This stage adds no code optimization; its
acceptance is linear source/graph work with indexed lookup and bounded memory
proportional to AST plus semantic facts. No inherited optimization gate applies.

## Handoff ledger

- Implementation remaining: resolve whole PA6 behavior groups above, then run
  PA6, prior-through, and file-audit checks without reducing fixture coverage.
- Independent review: verify semantic construction remains a reusable graph
  over AST source identities, and review C++11 interpretation/reference
  fixtures; no review question waives required behavior.
- Handoff boundary: only after required checks pass and the worktree is
  committed and clean; assignment completion remains subject to Ralph audit.
