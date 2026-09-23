# PA2 implementation and final audit

Stage base: c9aaae664fc3ac24bee05b55421bb145b2ba1494<br>
PA2 implementation: d90d01a319167350408e8c60da954f304b7293f4<br>
Buffered output: 3a604e4c8c2f01f2df2e5cce9216f037a2139652<br>
Final audit code fixes: 7737570def02b879c2e8b9b4c3892e30f6b189e2

## Final Spec Alignment

| Spec surface | PA2 result |
| --- | --- |
| Source and preprocessing-token flow | PA1's source cursor is reused. posttoken receives callbacks synchronously and keeps no complete token stream. It retains only the current maximal string-literal run required by phase 6. |
| Literal semantics and ABI view | Numeric, character and string tokens are classified by PA2 code; integer values and encoded literal units are written in the course x86-64 representation. UCN-produced backslashes now retain enough provenance to distinguish them from source escape sequences. |
| Canonical semantic graph, lookup, templates, demand and lowering | Not present in this staged token-description tool. PA2 makes no claims for those later production phases. |
| Optimization and native code | No IR optimizer or native-code generator is present. The only measured optimization is avoiding a flush per output token; generated-program runtime and text size are not applicable. |
| Complexity and memory | Tokenization and conversion are linear in source/token/output size, with bounded cursor lookahead. Retained token spellings are limited to the phase-6 string run; UCN backslash offsets add storage proportional to those UCNs. Output streams directly through std::cout and flushes at EOF. |
| Self-containment | PA2 results are produced by the implementation. Reference tools are observations only. No fixture or reference output was changed in this audit. |

PA2 is a staged frontend tool, not a production source-to-ELF path. Its textual
output is an explicit tool result; it is not an intermediate representation
transported into another production phase.

## Architecture review

dev/posttoken.cpp reads standard input in fixed-size blocks into one source
string. The source is then passed by const reference to PostTokenizeSource;
PPTokenizer and SourceCursor borrow it for the duration of the run.
SourceCursor performs UTF-8 decoding and translation phases 1–3 with bounded
character lookahead. The tokenizer emits each token spelling during its
callback. Non-string tokens are classified and written immediately. For
quoted tokens, the callback spelling remains borrowed; PA2 copies only tokens
in the pending maximal string run because their phase-6 result cannot be
known until a non-string token or EOF arrives.

Representative data flow:

    int item = 42;
    "a" "\u005Cn"_tag;

The pp-number 42 reaches PA2 as one callback, passes integer grammar,
suffix/range and ABI type selection, and is emitted as an int literal. The
two adjacent strings are copied into the pending run. The tokenizer exposes
their PA1 spellings with UCNs decoded and carries byte offsets for any UCN
that decoded to backslash. At flush, PA2 resolves the common encoding and
suffix, decodes each literal element, concatenates the encoded code units,
appends the terminator, then writes one literal result. The sideband preserves
the distinction between a UCN yielding a backslash and an escape sequence
beginning with a backslash; it does not alter the required printed source
spelling.

Numeric parsing uses checked accumulation and a bounded candidate list for
integer type selection. Character and string decoding use explicit escape,
Unicode-scalar and code-unit paths. Output is formatted directly; there is no
posttoken vector, syntax tree, textual token reparse or later semantic consumer
in this stage. The token spelling maps are classification tables, not semantic
identity keys. There are no parser checkpoints, template facts, worklists,
analysis caches, optimizer passes, MIR or ELF surfaces to audit in PA2.

Integer candidate order follows N3485 §2.14.2 and the course's Linux x86-64
type widths. The decimal floating grammar follows N3485 §2.14.4 and accepted
values pass through the PA2Decode functions required by the handout. Long
double output starts from a zeroed 16-byte buffer and copies the 10-byte
80-bit value, leaving the six SysV AMD64 padding bytes deterministically zero.

## Findings and changes

1. **Fixed UCN/escape conflation.** The previous tokenizer decoded
   \u005C inside a non-raw literal into a backslash byte, and PA2 then
   reparsed that byte as the start of an escape. The reduced input
   student.tests/pa2-ucn-backslash-literals.input includes
   "\u005Cn" and "\u005Cx41"_x. Before the fix these became newline and
   A; correct output contains the two characters backslash plus n, or
   backslash plus x41. It also checks a backslash character UCN, an invalid
   two-character character literal under the course rule, and a UDL string
   adjacent to an ordinary string.

   N3485 §2.2 phase 5 separately converts each source character, escape
   sequence and UCN in a literal. §2.14.3 and §2.14.5 define UCNs and escape
   sequences as distinct literal elements; §2.3 p.2 permits the basic
   backslash UCN in a literal. The reducer therefore proves the expected
   5C6E00 bytes from the language rules, rather than from compiler agreement.
   The tokenizer now carries offsets for UCN-produced backslashes through
   borrowed callbacks, and PA2 retains those offsets only with a pending
   string run. Existing one-argument callback implementations remain
   compatible through default forwarding methods.

2. **Narrowed literal-operator splitting.** The old operator context split
   every following user-defined string token into a string and suffix. N3485
   §13.5.8 defines literal-operator-id as operator "" identifier, so the
   contextual split now applies only to the exact empty "" spelling. The
   course case operator""sv remains split as required. The personal reducer
   also checks that operator"abc"_x remains a user-defined string token.

No other whole-stage defect was found in the reviewed integer suffix/type
selection, floating grammar, character escapes, raw literals, string encoding
and concatenation paths. No reference sidecar, required test, coverage bucket
or comparison rule was changed. Existing successful outputs and exit-status
oracles remain intact.

## Performance evidence

PA2 creates token descriptions, not executables. Generated-program runtime
and generated text size are not applicable. The measured compiler-stage
latency is the posttoken process; its executable text size is reported below.
There is no optimizer pipeline, so IR transform legality, analysis
invalidation, and pipeline growth budgets do not apply. The output flush change
has no semantic proof obligations beyond preserving bytes and completion
behavior; its output hashes match.

### Per-token flush removal

The frozen comparison used GCC 15.2.0 with -std=gnu++11 -Wall -O3, input repeated from
item 123 1.25 "x"; followed by a newline, and stdout redirected to /dev/null.
A is d90d01a3 (per-token std::endl, SHA-256
f07ce6eea9bb9c62e8743ed642268a2827d7c1b607d6bcea82c5bb23a34a06c8); B is
3a604e4c (newline plus one EOF flush, SHA-256
58ac32156214871a49c5696282279a1c5af33c9319f2e9cd430d5fe0adcc7745). The
1 MiB and 8 MiB inputs hash to
c4227e211bf690504274250dc2c83a075eedac1cc74dcc9b5b30282a19064af1 and
d8ae534f0c0f7c863f94a2d8b43de8b6b3e70000e7d5e0d3a22cc0ae2e7249ea.
All times are wall seconds and RSS is KiB.

| Workload / sequence | A observations | B observations |
| --- | --- | --- |
| 1 MiB A/A calibration | .77/4812, .76/4816, .76/4780 | — |
| 8 MiB A/A calibration | 6.15/11728, 6.14/12008, 6.23/12184 | — |
| 1 MiB ABBA 1 (A1 B1 B2 A2) | .79/4848, .78/4972 | .55/4744, .38/4828 |
| 1 MiB ABBA 2 (A1 B1 B2 A2) | .78/4740, .88/4816 | .40/4844, .39/4964 |
| 8 MiB ABBA 1 (A1 B1 B2 A2) | 6.48/12200, 6.74/12184 | 3.20/11936, 3.05/12116 |
| 8 MiB ABBA 2 (A1 B1 B2 A2) | 6.96/11944, 6.78/11944 | 4.23/11912, 3.55/12196 |
| 8 MiB ABBA 3 (A1 B1 B2 A2) | 6.46/12164, 6.29/11912 | 3.16/12156, 3.30/12096 |

The paired 8 MiB latency reductions were 52.7%, 43.4% and 49.3%; pooled
medians were 6.61 s for A (6.29–6.96) and 3.25 s for B (3.05–4.23). The 1 MiB
pooled medians were .785 s and .395 s. All ABBA outputs were byte-identical;
the 8 MiB output was 58,720,169 bytes with SHA-256
37b581255a37c3ada185cbd69de263c7638e98a7c059335e46b7879422698ed8.
The 8 MiB RSS ranges overlap: A 11912–12200 KiB and B 11912–12196 KiB.
Executable text was 181,720 bytes for A and 179,392 for B. These measurements
support the flush-removal latency improvement; no generated-program claim is
made.

### Final provenance fix versus the frozen buffered tool

A is the frozen buffered implementation above (58ac3215); B is the final
audited dev/posttoken binary, SHA-256
c8b7e83177fb405c92bd8154b42cf85315ea95059586a2bee4ddbaf5b5f7108f. Both
use GCC 15.2.0 and the default -std=gnu++11 -Wall -O3 build mode. The 8 MiB input, output
hash and timing method are unchanged. The three A/A blocks used order
A1,A2,A2,A1; final ABBA order was A1,B1,B2,A2.

| A/A block | A1 | A2 | A2 | A1 |
| --- | --- | --- | --- | --- |
| 1 | 3.47/11900 | 3.18/11996 | 3.20/12004 | 3.12/11932 |
| 2 | 3.26/12000 | 3.09/12008 | 3.11/12132 | 3.12/12196 |
| 3 | 3.10/12104 | 4.55/12196 | 3.31/12024 | 3.14/11968 |

| ABBA block | A1 | B1 | B2 | A2 |
| --- | --- | --- | --- | --- |
| 1 | 3.08/11996 | 3.13/12144 | 3.06/12012 | 3.10/12004 |
| 2 | 3.16/11976 | 3.06/11980 | 3.07/12208 | 3.10/12196 |
| 3 | 3.02/11992 | 3.15/11968 | 3.15/12008 | 4.11/11976 |

Paired B-minus-A latency differences are +0.005, -0.065, -0.415 seconds
(median -0.065); paired RSS differences are +78, +8, +4 KiB. A/A paired
latency differences (A2 minus A1) are -0.105, -0.090, +0.810 seconds. The
spread, including the 4.55 s and 4.11 s observations, is larger than the
measured B/A differences, so this comparison supports no latency or memory
claim. It discloses a possible small runtime movement without treating noise
as a regression gate.

The final 8 MiB output matches A byte-for-byte and has the same hash above.
The frozen buffered tool has .text/.data/.bss of 179392/3112/1744 bytes;
final B has 183808/3144/1744, a 4416-byte (2.46%) text increase for the
correctness metadata and callback paths. Generated text size remains not
applicable.

## Handoff ledger

- **PA1 → PA2:** the callback spelling is borrowed. PA2 consumes it
  synchronously and copies only deferred phase-6 strings. The UCN provenance
  reducer now exercises decoded spelling plus semantic use across this
  boundary. PA2 does not consume source locations.
- **Unaudited later consumer:** source-location callbacks and any new PA3/PA4
  token consumers still need integration review when those stages adopt the
  interface. ppexpr and preproc do not currently link the shared tokenizer;
  PA2's printed token format must not become their production transport.
- **Later spec surfaces:** semantic graph, lookup, templates, demand,
  source-to-LowIR, optimization, MIR, allocation and ELF remain owned by their
  later milestones; PA2 has no implementation surface for them.
- **Benchmark suite:** PA2's fixed token-tool workload is the only applicable
  executable-stage benchmark here. The fixed compiler and generated-program
  suite for templates, loops, calls, memory, floating point and self-hosting
  remains to be established and audited with the compiler/native milestones.

## Validation

- Personal reducer:
  dev/posttoken < student.tests/pa2-ucn-backslash-literals.input | diff -u student.tests/pa2-ucn-backslash-literals.expected - — pass.
- perl scripts/cppgm_file_audit.pl --stage pa2 --paths dev/src — pass,
  20 files checked.
- make test-report-through-pa2 — pass, 80/80 tests, 2/2 stages; earlier PA1
  and PA2 both pass.
- git diff --check — pass.
- No required fixture, output comparison, coverage or timeout expectation was
  removed or changed.
