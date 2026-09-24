# PA7 implementation handoff

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

## Handoff ledger

- **Unfinished PA7 implementation:** none in the required slice. General
  template bodies, non-type template arguments and member templates remain
  future-stage work, consistent with PA7's out-of-scope boundary.
- **Independent audit:** retain whole-stage review of target-directed overload
  resolution, derived-to-base cast materialization, deferred member-definition
  ordering, and extension of cached AST-node facts through later class-aware
  stages. These are review markers, not waived requirements; no new known defect
  remains in the required PA7 slice.
- No reference corrections were needed. Ralph's full-stage audit must resolve
  any whole-stage findings before advancement.
