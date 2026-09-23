# PA1 implementation and final audit

Stage base commit: `e50e87639188c5da678ab00d4d927c5d57396c65`<br>
PA1 implementation commit at audit start: `0142ff76e6051cb995d8a205ea57d00595bcd96a`<br>
Final audited input reader: `dev/pptoken.cpp` SHA-256
`79f4d6fd23897e5975d2969de1cc36a448b81fb6679edfaf29e5cd4bed0e4f58`.

## Final Spec Alignment

| Spec surface | PA1 result |
| --- | --- |
| Source bytes, phases 1–3, preprocessing tokens | Implemented in the shared tokenizer and its callback interface; representative flow and ownership are reviewed below. |
| Streaming token production and lifetimes | Tokens are emitted immediately. Code point lookahead is bounded; the tokenizer does not retain a token vector. The duplicate full source staging at the command boundary is fixed. |
| Canonical semantic graph, lookup, templates, demand, lowering, optimization, MIR and ELF | Not present in PA1. No whole-compiler or source-to-ELF alignment claim is made here; these surfaces require review in their owning milestones. |
| Stage complexity and performance evidence | Input/token work is O(source bytes + emitted token bytes), with bounded code point lookahead. No numeric PA1 latency or RSS budget exists. The historical size-doubling ratio is descriptive, not an exit gate. Current relative latency evidence is inconclusive under A/A noise; see below. |
| Generated executable runtime and text size | Not applicable: PA1 emits token descriptions and does not compile or link programs. The `pptoken` tool text size is measured below. |
| Self-containment | PA1 output comes from `pptoken` and the shared implementation. The reference executable supplies observations/oracles only; the implementation does not invoke it. |

## Architecture review

`pptoken` reads standard input in 64 KiB blocks in `ReadStandardInput`, returns
one source string, and binds it as a const string for tokenization. That string
is the sole retained source buffer and remains alive until `PPTokenizer::run`
finishes. This replaces the prior `ostringstream` plus `str()` source copy.
Read failures are reported before tokenization.

`SourceCursor::readRaw` validates UTF-8 and tracks byte offsets and physical
locations. `readPhase1` performs trigraph replacement and supplies the
standard-required final newline; `readLogical` removes backslash-newline
splices. The raw, phase-1 and logical deques hold only bounded lookahead. The
largest lexical lookahead is ten code points for a `\UXXXXXXXX` universal
character name; no phase-wide character or token buffer is built.

`PPTokenizer::run` consumes the logical cursor and emits each event through
`IPPTokenStream`. It recognizes directive context before treating an include
operand as a header name. `scanWhitespace` folds comments into whitespace and
preserves line feeds inside block comments. Identifier, pp-number and quoted
literal paths decode universal character names where their grammars require
them. `scanRawLiteral` scans the original source for the raw terminator and
resets the cursor after the raw spelling, preserving transformations reverted
inside raw literals. At most the token currently being emitted has an owning
spelling; the callback argument is borrowed until the callback returns. The
debug stream writes it synchronously.

Representative end-to-end input:

```cpp
??=include <unit.hpp>
int joined\
Name = 1e+2; /* before
inside */ auto id = na\u00E9me; auto raw = R"d(??/ \u0041
raw)d";
```

The cursor turns `??=` into `#`, joins `joined` and `Name`, and keeps the block
comment's line feed as a `new-line` event. Directive state makes `<unit.hpp>` a
`header-name`; pp-number scanning emits `1e+2`; UCN decoding emits identifier
`naéme` in UTF-8. The raw token retains `??/` and `\u0041` and its embedded
line feed. `DebugPPTokenStream` prints each callback's type and byte length,
then prints `eof`. This exercises source decoding, phase ordering, contextual
tokenization, token spelling and output ownership in one flow.

There are no PA1 parser, semantic, template, optimization or native-code
surfaces to audit for canonical keys, demand invalidation, optimization
legality/profitability, pipeline growth, ABI or ELF emission. PA1 is aligned at
its own boundary and does not claim the spec's later source-to-ELF trace.

## Findings and changes

- **Fixed:** the command entry point kept an `ostringstream` buffer while
  `str()` supplied another full source string. It now reads in fixed-size
  blocks into one string and passes that immutable buffer through the
  tokenization run. The source ownership and release boundary are explicit;
  token spellings and output remain one-at-a-time.
- **No other whole-stage defect found:** code inspection, the representative
  flow, all required fixtures and the file audit agree. Translation and token
  rules, reference coverage, exit-status oracles and result comparisons remain
  intact.

### Reference corrections

The two checked-output corrections already present in the PA1 implementation
were independently reviewed. The pinned reference bundle source is
`c2f713cd70d06170632bfde3e75dd6fe1aa44d98` (bundle SHA-256
`c532a109ae800825da24f60ae28ea894aa4896f56efb6ebb14728cdccf25a7d7`). The
reducer `student.tests/pa1-comment-newlines.input` is
`a/* comment\ncontinued */b\n`. N3485 §2.2 paragraph 3 requires each comment
to be replaced by one space and says new-line characters are retained. The
reducer proves the pinned output omitted the comment's line feed; the same
omission occurs in `100-extra-comments.ref` and `900-real-world.ref`. Only
those two successful-output sidecars gained the required `new-line` events.
Inputs, exit-status sidecars, required cases, coverage and comparison rules
were not changed. See [N3485 §2.2](../doc/n3485.txt) for the contract text.

The `1p+3` boundary remains three tokens under N3485 §2.10: a pp-number may
take a sign after `e` or `E`, not after `p`. The personal trailing-backslash
reducer also remains valid: N3485 §2.2 paragraph 2 appends a newline to a
nonempty file without one, then phase 2 splices the final backslash-newline.

## Behavior ledger

| Group / owner | Data flow and bound | Validation | State |
| --- | --- | --- | --- |
| Source decoding and translation / `SourceCursor` | UTF-8 bytes → code points → trigraph/splice cursor; O(source bytes), bounded lookahead | UTF-8, BOM, trigraph, UCN ordering, splice, EOF and trailing-backslash fixtures/reducers | Pass |
| Whitespace, comments and include context / `PPTokenizer` | Cursor → newline/whitespace/header events; O(source bytes) | Comment newline reducer; header-context and comment fixtures | Pass |
| Token formation / `PPTokenizer` | Cursor → identifiers, pp-numbers, literals, punctuators and fallback characters → synchronous callbacks; O(source bytes + emitted bytes) | All PA1 required token and malformed-input fixtures | Pass |
| Callback output and source locations / `IPPTokenStream` | One borrowed spelling per callback; physical location set before each event | Debug stream; static compatibility review of default `set_source_location` calling virtual `set_source_line` | Pass in PA1 |

## Performance evidence

PA1 has no source-to-object compile latency or generated executable. The
available phase latency is the standalone `pptoken` process time; its peak RSS
and tool text section were measured. Executable runtime and generated-program
text size are N/A. No optimization or runtime-benefit claim is made.

The prior plan's seven-sample timings are retained as historical descriptive
measurements only. They had no frozen A/B or A/A calibration and do not support
a comparative claim or a numeric gate:

| Input | Wall seconds (all observations) | Median | Peak RSS KiB |
| --- | --- | ---: | ---: |
| `900-real-world.t`, 4,134 bytes | 0.004406, 0.004265, 0.003986, 0.003972, 0.004949, 0.003971, 0.003911 | 0.003986 | 3,584 |
| 4,096 generated lines, 430,080 bytes | 0.128147, 0.132485, 0.128210, 0.129715, 0.129907, 0.128371, 0.127907 | 0.128371 | 4,272 |
| 8,192 generated lines, 860,160 bytes | 0.256970, 0.254974, 0.251946, 0.252702, 0.255313, 0.258014, 0.262856 | 0.255313 | 5,164 |

The old 1.99x time ratio for a 2x input increase was only a descriptive
observation. PA1 has no numeric latency/RSS threshold; the applicable bound is
linear work, established by the bounded cursor and token paths above.

For the input-reader change, frozen A is the standalone implementation at
`0142ff76e6051cb995d8a205ea57d00595bcd96a`; frozen B is the audited reader.
Both were built directly from the entry point and tokenizer source with GCC
15.2.0 and `-std=gnu++11 -Wall -O3`, without the test-runner wrapper. Each
benchmark run used the same 4,200,000-byte deterministic input (40,000 copies
of `int value_17 = 0x1234 + count_17; const char* text_17 = "token stream";
value_17 += count_17; // comment`), streamed output to `/dev/null`, and used
GNU `time` wall seconds and peak RSS. Each run took multiple seconds, so process
startup was small relative to the workload. The input SHA-256 is
`3fe312252155f9a3fadc52c98c398a02d8786243c522f3841bedd2f1347d9439`. A and B
produced byte-identical 31,280,004-byte token output (SHA-256
`fd2a63009abf4f82febd07a1218d45ca9cf195c43fc214621c2001dd81f9f73a`). Binary
SHA-256: A `76066469b2b1f0806903e0f8b4aefaee43439fc771d99cd4f517c7b88cbb2a79`,
B `ac0dfdbdf678fba14785e15f08f13258a359bcc20db07fc24d2e331541ec8f67`.

`size` reports A text/data/BSS `82,852 / 1,832 / 1,152` bytes and B
`82,982 / 1,808 / 1,152` bytes. The tool text section grows by 130 bytes
(0.16%); this is tool size, not generated program size.

Noise calibration ran identical A binaries in four `A1,A2,A2,A1` blocks; A1
and A2 had identical hashes. The final comparison ran eight balanced
`A1,B1,B2,A2` blocks; B1 and B2 also had identical hashes. Each cell below is
`wall seconds / peak RSS KiB`; every observation is retained.

| A/A block | A1 | A2 | A2 | A1 |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 2.25 / 11828 | 2.58 / 11856 | 2.32 / 11824 | 2.27 / 11824 |
| 2 | 2.41 / 11828 | 2.66 / 11880 | 2.16 / 11984 | 2.32 / 11824 |
| 3 | 2.44 / 11824 | 2.68 / 11852 | 2.04 / 11796 | 2.42 / 11796 |
| 4 | 2.36 / 11828 | 2.21 / 11824 | 2.25 / 11824 | 2.35 / 12020 |

| ABBA block | A1 | B1 | B2 | A2 |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 3.11 / 11604 | 2.13 / 11692 | 2.25 / 11356 | 2.02 / 11888 |
| 2 | 2.12 / 11792 | 1.92 / 11692 | 2.12 / 11660 | 2.16 / 11828 |
| 3 | 1.94 / 11856 | 1.93 / 11692 | 2.21 / 11656 | 2.49 / 11828 |
| 4 | 2.38 / 11792 | 2.38 / 11660 | 2.24 / 11688 | 2.65 / 11824 |
| 5 | 3.02 / 11884 | 2.50 / 11680 | 2.37 / 11688 | 2.39 / 11792 |
| 6 | 2.49 / 11824 | 2.49 / 11660 | 2.35 / 11656 | 2.69 / 11824 |
| 7 | 2.66 / 11824 | 2.65 / 11688 | 2.53 / 11660 | 2.51 / 11880 |
| 8 | 2.69 / 11884 | 2.51 / 11672 | 2.38 / 11692 | 1.75 / 11824 |

The A/A block mean differences (A2 minus A1) were `+0.190, +0.045, -0.070,
-0.125` seconds. ABBA paired wall differences (B mean minus A mean) were
`-0.375, -0.120, -0.145, -0.205, -0.270, -0.170, +0.005, +0.225` seconds;
the median was `-0.1575` seconds, with a `-0.375` to `+0.225` spread and two
blocks favoring A. The A/A spread is too large to support a latency claim, so
the result is inconclusive and no speedup is accepted.

ABBA block-mean RSS differences (B minus A) were `-222, -134, -168, -134,
-154, -166, -178, -172` KiB; median `-167` KiB, range `-222` to `-134`, with
all eight blocks favoring B. A/A block-mean RSS differences ranged from `-100`
to `+106` KiB. This supports a modest peak-RSS reduction for this input, but
not a broad workload claim. The fixed-source ownership change is also directly
visible in the code. No generated-runtime or generated-text result exists at
PA1.

## Handoffs and validation

- No unfinished behavior group remains in PA1. The implementation commit
  already includes the PA1 source, source-set registration, reducers and
  justified reference corrections.
- `IPPTokenStream` documents that spelling references are callback-borrowed;
  consumers that retain a token must copy or intern it during the callback.
  The default location hook forwards to legacy `set_source_line` overrides.
  `rg` found no PA2–PA4 production consumer in this checkout, so this is a
  reviewed interface contract, not an exercised integration. PA2/PA4 should
  verify retained spelling and physical locations when they add consumers.
- The broader source-to-ELF declaration/template trace, semantic fact keys,
  demand invalidation, optimization budgets, executable benchmarks and backend
  allocation remain future milestone audits; PA1 has no such implementation
  surface.
- `perl scripts/cppgm_file_audit.pl --stage pa1 --paths dev/src`: pass, 18 files.
- `make test-report-through-pa1`: pass, 54/54 tests, 1/1 stages; no timeout or
  failure reported.
- `git diff --check`: pass. Final commit and clean worktree are recorded by the
  enclosing audit result.
