# Packed single-precision power qualification report

The replacement obligation remains **PARTIAL** across the entire supported source/platform scope. Native Windows ARM64 release-CRT profiles now use an admitted packed `VectorPow` implementation, with one explicit scalar fallback when native input-denormal traps are enabled. Debug CRT linkage (`_DEBUG`), ARM64EC, x64 and non-Windows profiles retain their original scalar calls. Linux execution and native x64 enabled-trap qualification were unavailable here. The earlier SIMD conversion commit `3299b121c` and completed alignment repairs remain retained evidence.

## Admitted native Windows ARM64 implementation

The implementation in `global/simd/vector_pow_windows_arm64.h` adapts the Arm optimized-routines logarithm/exp2 tables and polynomial order to four binary32 inputs using packed binary64 pairs, FMA, integer masks, packed rounding/conversion and a single narrowing step. Durable MIT attribution identifies official upstream commit `f67e7daa5457646fad9f75580263dba654c988aa`; the full notice is adjacent in `vector_pow_windows_arm64_license.txt`. No compiler vector-library declaration, scalar intrinsic broadcast, runtime DLL fingerprint, new dependency or service supplies the numerical work.

Actual native execution resolved `powf` to `C:/Windows/System32/ucrtbase.dll`, version **10.0.28000.3231**, native entry RVA **0x5abf0**. The loaded 1,000-byte entry matched the corresponding image bytes. All 64 table qwords and five logarithm coefficients matched the pinned primary Arm source. This establishes a measured provider correspondence; static SDK symbols alone were not used to identify the executing implementation. The Windows fast returns, older range helpers and operation/errno order were checked against the actual provider rather than copied blindly from the portable source.

The bounded path admits positive normal x in [2^-16, 2^16] and normal |y| <= 4, plus exact zero/one shortcuts whose inactive inputs are sanitized to 1^0 and restored with integer masks. Its power exponent lies within [-64, 64], so generic range handlers are absent from that path. The generic packed path preserves other numerical and special cases. Enabled standard exceptions process active lanes in the original observed W,Z,Y,X order and preserve every primitive's sticky-state and errno timing before raising the first enabled category.

Native FPSR matters beyond standard FE flags. The CRT normalizes a subnormal x before its y == 1 return: skipping that multiply loses IDC under flush mode while leaving result bits and standard flags unchanged. The implementation retains this packed normalization side effect. Per-operation restoration merges prior/new IDC; a separate QC saturation-bit probe established that the CRT environment functions preserve QC. Whole-corpus tests seed IDC/QC independently rather than inferring native status from FE_ALL_EXCEPT.

Native input-denormal exception enable (FPCR bit 15) is an explicit remaining scalar path. A bounded primitive experiment observed scalar FMUL reporting Windows DENORMAL **0xc000008d**, while packed FMUL on the same flush-mode subnormal input reported INVALID **0xc0000090**. A native packed replay therefore fails the exact trap-category gate. The dispatcher sends only this native-enabled state to the original four-Pow expression, retaining exact evaluation form/order. No synthetic exception handler or fabricated category replaces the provider behavior.

## Retained release numerical and native-state qualification

Both compilation profiles derive from the actual ARM64 math-test `opt` command, retaining `-O2`, target, defines, includes and code generation. The separately labelled strict profile additionally uses `-ffp-model=strict`. Each child runs the entire enumerated 506,124-vector / 2,024,496-lane corpus in all four rounding modes, including every declared exceptional pair/lane, random and positive-finite inputs, every 8-bit sRGB decode input, the complete 16-bit uniform linear grid and all 33 adjacent binary32 inputs at every quantization transition.

The initial release-admission isolated candidate SHA was `399353745164eeb9b7850a5483938b10907e5b6c138fda5ff9ef4a0590517a47`. Across strict and actual opt, **2,048** full candidate corpus children cover all 32 initial standard FE sticky states, errno seeds 0/7/33/34, native IDC/QC seeds 0/IDC/QC/both and both actual DN_SAVE/DN_FLUSH modes. **16** independent original-production control children also pass. Every result-bit, standard/mapped/native-status, errno and 8-bit quantization difference count is **0**.

An expanded native-state matrix runs **40,960** cases: both profiles, both denormal modes, native input-denormal enable 0/1, QC 0/1, all 32 standard exception-enable masks, initial IDC 0/1, all four rounding modes and ten mixed/fast-return cases. It compares raw FPSR/FPCR, mapped CRT status/control, first delivered exception category, errno and return behavior. Every difference count is **0**. A further **512** seeded original stronger trap subprocesses preserve invalid/overflow/divide-by-zero controls and both mixed lane orders.

Fresh public-API probes remove the isolated candidate include entirely. They compile a complete current global-header overlay and call `Production -> VectorPow -> SIMDVectorPowWindowsArm64Detail::Pow`. Strict/opt public qualification adds **16** full fixed corpus children, **16** frozen-reference controls, **40,960** expanded raw native cases and **512** seeded stronger trap children, all exit **0** with **0** differences. Forty before/after function comparisons establish identical executed code bytes, normalized code-target relocations and referenced constants from isolated final source to public headers and then the naming/format-only final header. Physical symbol offsets differ only between merged text and inline COMDAT placement, with target function bytes checked separately. This preserves the larger seeded proof without rerunning unchanged arithmetic merely for formatting.

The initial release-admission public header SHA was `cfcbc35b9ab8e7f56c7fde1885796aca7438b8a6a3a58f9119af0ab52fba176f`. The reviewed six-file patch SHA is `e1ebf42648293074264cde9444ce45643d42c3e439b1ceb89bdbf8410be37f56`. Two default edge GTests were compiled and linked through the same proposed public header, then exercised successfully; all 22 reused cached object/library inputs remained byte-identical. The tests cover mixed specials, sRGB-adjacent and bounded/endpoint packets, subnormal y == 1 status, seeded standard/native state and the enabled-invalid ordered path.

Fresh strict, actual-opt and timing disassembly confirms **0 scalar floating arithmetic/conversions and 0 libm power calls** in the packed private functions, including the new subnormal normalization. Explicit IDE fallback and the frozen reference each contain four real powf calls. The public wrapper tail-branches to the private entry; final timing includes that wrapper. The standard exception replay helper's scalar operation belongs to CRT `feraiseexcept`, and is classified separately as required exception delivery rather than numerical power evaluation.

The complete final-proof log scan covers **3,191** text logs. It classifies **174,080** expected SEH observations, **8,384** exceptional-corpus identifiers, **126** disassembly-symbol/instruction matches and **11** retained historical source-diff tokens. Unexpected diagnostics: **0**. All final compile/link commands exit **0**, with **0 compiler errors and 0 warnings**. Earlier failed experiments remain rejection evidence and are not counted as current production defects.

## Debug rejection and admitted enabled-underflow repair

The first canonical debug execution exposed a real provider mismatch that release-only overlays did not establish. Its mixed-power edge test failed 336 assertions in the subnormal-base y == 1 packet; the other two directly selected tests passed. The actual canonical opt image passed the same three tests and its public production corpus/trap gates. These failed debug executions remain historical rejection evidence.

Actual debug linkage defines `_DEBUG`, requests `msvcrtd`, and imports `powf` from `C:/Windows/System32/ucrtbased.dll`, version **10.0.26100.7705**, SHA `23a027e0d6637ce231a39397363436ca048043f05b24839d304769ca88d7090a`, entry RVA **0x15e4b0** (904 bytes). This differs from release `ucrtbase.dll` version 10.0.28000.3231. All 64 table qwords and normalized scalar coefficients match, but debug omits both finite-one shortcuts, evaluates x + y before selected special returns, and assigns ERANGE after exceptional operations. Actual loaded bytes/imports establish this difference; optimization level and static SDK names do not identify the provider.

An isolated debug adaptation reproduced those differences and passed **2,048** full fixed corpus children under actual `-O0`/debug-CRT and the same debug command plus strict floating-point mode. All **32,768** category/mode rows have zero result-bit, standard/mapped/native-status, errno and quantization differences; every child observes 506,124 vectors / 2,024,496 lanes. Additional y == 1, x == 1 and y == 0 boundary families cover 196,608 vector packets per rounding mode, including arbitrary normal and subnormal bit patterns. The changed ordered path also passes native-control, seeded stronger-trap and focused prior-sticky-state checks.

Correctness did not admit that debug implementation. Actual debug policy intentionally leaves `NWB_INLINE` as ordinary inline, and the packed adaptation retained 78 private helper instantiations rather than the optimized provider's short scalar routine. Serial public-API timing on logical CPU 8 uses 4,096 warm-up vectors and seven alternating pairs of 65,536 vectors for all four datasets and both throughput/dependent latency. All **56** measured pair checksums and **8** warm-up comparisons match. Candidate/reference median throughput ratios are **10.554**, **10.705**, **10.763** and **11.416** for mixed, sRGB encode, sRGB decode and RGBA endpoints; latency ratios are **9.320**, **9.242**, **9.243** and **6.829**. This material regression rejects debug promotion. Global inline/configuration policy is unchanged, and no dormant debug algorithm is shipped.

The final dispatch explicitly excludes `_DEBUG` linkage and preserves the original four-Pow backend expression, source-identical to commit `3299b121c`. The current release-only private header SHA is `0430fce22710be8696a517f0a15b229a1e3a5be97144fdf94cf1ff6d25c5bf56`; the public dispatcher SHA is `a1db6aa70dc07cc8c885ac4801a4e64a65c57e80135150e7d043e1663b8cc09c`. Debug remains an explicit scalar remainder. Fresh final public probes under actual debug/strict-debug and opt/strict-release compile/link successfully and each pass the entire fixed corpus plus the original five and new 48 native trap-edge comparisons. Both default power edge GTests pass through fresh isolated cached-object links for actual debug and opt, with cached inputs unchanged.

The release repair retains one independently discovered correctness change. `.5^149` and `.5^127` can trap underflow on scalar conversion even when masked exact conversion raises no sticky underflow flag. Ordered primitive replay now recognizes exact subnormal arithmetic results; narrowing tests nonzero magnitude below binary32 minimum-normal before conversion/inexact replay. Flush mode consumes binary64 subnormal inputs as zero, so those inputs are excluded from pre-conversion underflow admission. This affects the rare enabled-exception path and preserves the ordinary masked arithmetic.

Independent primitive qualification covers **557,056** unique final scalar-FCVT/FADD versus packed/real-CRT-replay state comparisons across actual debug/release linkage, all four rounding modes, both denormal modes, all 32 standard enable masks, initial IDC/QC, initial IEEE sticky seeds 0/inexact/underflow/both and errno seeds. Every final difference is zero. Its **98,304** delivered scalar-underflow cases match legitimate CRT replay in category, mapped/raw flags, errno and mapped/raw control. An earlier too-broad flush-mode preguard remains historical rejection evidence.

The permanent opt-in `traps` command retains the unchanged original `VerifyTraps` and adds an adjacent **48-state** edge gate on native ARM64. Exact-tiny `.5^149`/`.5^127` and subnormal-base zero-exponent inputs run with underflow and underflow-plus-inexact enabled, all four rounding modes and both denormal modes. Category, mapped/raw flags, errno and mapped/raw control must agree. The frozen `Reference`, `Compare`, `Verify` and original `VerifyTraps` blocks are byte-identical in normalized source. Their numerical formulas and tolerances were not changed.

Final code-target/constant identity checks preserve **10/10** repaired release functions before and after project-trait cleanup across strict/opt and **6/6** original measured release timing functions, including the ordinary packet, private dispatcher, original fallback, public wrapper/reference and timing loop. Earlier identity chains retain the full release 2,048-child numerical proof and 112 measured timing pairs. The changed ordered release path receives fresh native/entry-state proof. Debug candidate arithmetic is not claimed as deployed simply because its numerical gates passed.

All new diagnosis/proof/identity/timing commands and full logs are under `__cmake/Testing/task4_pow_dbg_diagnosis`; independent provider/primitive evidence is under `task4_platform_dbg_provider_review` and `task4_platform_fp_tiny_probe_*`. The first-admission ARM producer remains immutable historical release evidence; new source snapshots and execution hashes are separate. Canonical post-repair builds and direct executable closure belong to the build owner and are recorded separately.

The complete new diagnosis log inventory contains **4,073** text logs (139,855,532 bytes). Full reads classify **727,180** expected SEH observations, **8,488** exceptional-corpus identifiers, **504** disassembly identifiers, **82** import-symbol identifiers and **21** diagnostic-token lines from the rejected initial standalone alias fixture. Unexpected or unclassified diagnostic hits: **0**. All 16 current reviewed compile/link/corpus/trap command logs exit **0**; current compiler errors/warnings: **0/0**. Historical rejected candidate runs, exploratory harness failures and the earlier canonical debug failures remain labelled rather than being counted as current clean passes. `full_log_scan_final.json`, `release_final/reviewed_current_log_audit.json` and the independent primitive producer retain the classifications and hashes.

## Matched public-API timing

Timing ran serially on allowed logical CPU 8 (affinity 256), Snapdragon X2 Elite Extreme X2E94100, native Windows ARM64, actual opt flags and matched default control state. Each dataset uses 4,096 warm-up iterations followed by seven alternating pairs of 1,048,576 vectors for independent throughput and dependent latency. All four lane outputs feed the checksum; dependent inputs vary from preceding output bits. The frozen reference emits four real powf calls inside each evaluated vector, and the loop does not omit or hoist those calls. One exact same-binary repeat resolves endpoint variation. All **112 measured pair checksums** and all **16 warm-up comparisons** match.

Ratios below are candidate/reference elapsed time; values below 1 are faster. Brackets contain the full seven-pair range, including retained slow-reference outliers.

| Dataset | Throughput first / repeat median [range] | Dependent latency first / repeat median [range] |
| --- | --- | --- |
| Mixed positive bases/exponents | 0.792 [0.409, 0.803] / 0.801 [0.583, 0.860] | 0.947 [0.920, 0.948] / 0.945 [0.931, 0.955] |
| Fixed sRGB encoding exponent | 0.801 [0.781, 0.828] / 0.796 [0.790, 0.826] | 0.942 [0.897, 0.948] / 0.941 [0.915, 0.945] |
| Fixed sRGB decoding exponent | 0.791 [0.725, 0.800] / 0.799 [0.787, 0.829] | 0.944 [0.939, 0.967] / 0.943 [0.925, 0.950] |
| Mixed RGBA zero/one endpoints, alpha 1 | 1.008 [1.003, 1.027] / 1.020 [0.999, 1.062] | 0.989 [0.971, 1.014] / 0.991 [0.935, 1.008] |

The measured ordinary throughput improves about 20% and dependent latency about 5–6%. Endpoint throughput trades roughly 0.8–2.0% for that ordinary gain while endpoint latency remains near parity. Admission accepts this small measured tradeoff; it does not claim a universal gain, consumer end-to-end FPS gain, or speed on generic exceptional inputs, other CPUs, denormal modes or enabled-exception slow paths. Earlier materially slower prototypes were rejected before this implementation was admitted.

## Current evidence locations and remaining limits

`__cmake/Testing/task4_pow_arm_crt_adaptation` retains actual-provider/source inventories, licensed primary source copies, exact compiler/linker/run commands, all final corpus/control/trap logs, native primitive rejection evidence, function identity proofs, final public disassembly, raw timing pairs, source snapshots and style/application manifests. Key manifests are `final_native_results.json`, `public_results.json`, `public_arithmetic_identity.json`, `final_public_emitted_code.json`, `final_native_public_full_log_scan.json`, `production_applied_sources.json` and `timing_public_final_opt/{results,summary,repeat01}.json`. Historical source hashes remain attached to their own frozen snapshots; an older source-diff context is not relabelled as execution of current code.

The native ARM64 release-CRT implementation is admitted for the measured Windows profile only. Finite corpora are not proofs over every binary32 pair or every CRT release. Debug CRT linkage and native input-denormal traps explicitly retain scalar powf. ARM64EC, x64 and other platforms still retain scalar powf; a masked x64 emulator experiment cannot establish native x64 flags/trap priority. Linux could not be built or run in this environment. Canonical post-integration builds, default tests and selected runtime checks are owned by the platform/build workers and recorded separately; pre-integration overlay qualification is retained with its exact provenance.

## Earlier rejected generic candidates and retained alignment evidence

The following sections retain the earlier checkpoint, before the provider-faithful native ARM64 implementation described above. The two generic packed polynomial candidates failed the exact contract and remain local experiments. Their production control used the original scalar `VectorPow`; those historical executions are not relabelled as measurements of the new implementation.

## Scope and implementation

The preserved reference copies the original four-lane `Pow(float, float)` expression. The production control calls public `VectorPow`. A local single-precision candidate composes packed `VectorLog2`, multiplication and `VectorExp2`. A second candidate performs binary32 mantissa/exponent reduction, a degree-31 reduced atanh logarithm series and degree-20 exponential series in packed binary64 lanes, then narrows once. It uses packed integer masks for signs, integral-exponent parity, zero/infinity/NaN result shapes and provisional `errno` handling. Candidate special-value side effects, range reporting and enabled exception priority are tested rather than assumed correct from the masks.

The candidates and all qualification support live under `tests/performance/simd_optimization`. No public math contract, tolerance or existing scalar oracle was relaxed. No candidate is registered as a passing CTest test. No performance measurement was attempted after correctness rejection.

LLVM 23's CPU `__builtin_elementwise_pow` was also inspected independently. The actual ARM64 and x64 `opt` command profiles emit four `powf` calls. That builtin retains scalar libm work and is not a packed alternative. Its compiler commands, assembly and zero-diagnostic logs are under `__cmake/Testing/task4_summary/elementwise_pow_*`.

## Environment and profiles

The host is Windows ARM64 on a Snapdragon X2 Elite Extreme X2E94100 (18 logical CPUs). ARM64 executables run natively. Windows x64 executables run under this host's x64 emulation; the observations are not native x64 hardware qualification.

The runner derives each compile command from the existing target architecture's `nwb_math_tests` `opt` entry in `compile_commands.json`. It retains `-O2`, target/ISA, includes, defines and normal code-generation settings, replacing only the source/output and removing `CMAKE_INTDIR`. It records the exact original entry and derived compile/link argument vectors in `results.json`. The separately labelled strict profiles add `-ffp-model=strict`; they do not substitute for the actual `opt` profiles. Each profile builds into a separate evidence directory and never configures or builds the shared CMake trees.

All four compilations and links exited **0**, with **0 compiler errors and 0 warnings** in the complete logs. `llvm-objdump` exited **0** for all four objects. The packed-double function contains packed binary64 arithmetic in both target architectures; saved function disassembly has 32 matched packed F64 arithmetic operations on ARM64 and 16 on x64. These counts establish packed arithmetic, not speed or numerical equivalence.

## Entire enumerated corpus

Each variant and profile completes 506,124 vector observations, or 2,024,496 result lanes, across nearest, downward, upward and toward-zero rounding. Per rounding mode:

| Corpus | Vector observations | Result lanes | Coverage |
| --- | ---: | ---: | --- |
| Exceptional | 3,364 | 13,456 | Cartesian product of all 29 declared input patterns in each of four lane positions; other lanes are identity controls. |
| Arbitrary | 32,768 | 131,072 | Deterministic arbitrary input-bit pairs in all four lanes. |
| Positive finite | 65,536 | 262,144 | Deterministic positive-finite bases and finite moderate exponents in all four lanes. |
| sRGB | 24,863 | 99,452 | All 256 8-bit decode inputs, all 65,536 16-bit uniform linear inputs, and all 33 neighboring binary32 values at each of 255 quantization transitions (repeated across all four lanes). |

The transition search executes the independent preserved powf/consumer quantization expression under the active mode. Consumer compile-time constants are `constexpr`, including the encoding exponent and decoding scale. Result bits, accumulated floating-point flags, `errno` and quantized channels are counted separately. The corpus is finite and does not prove equivalence over all binary32 input pairs. Once a concrete mismatch exists, the candidate is rejected regardless of the number of other passing inputs.

## Completed results

Every production control has zero bit, flag, `errno` and quantization differences in all four rounding modes and all four profiles. Both packed candidates return **1** from masked qualification in every profile.

Counts below aggregate all four modes. Bits and quantized counts are differing lanes/channels; flags and `errno` counts are differing vector observations. The columns overlap and must not be interpreted as counts of distinct failing inputs.

| Profile | Variant | Bits | Flags | errno | Quantized channels | Masked / trap exit |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| arm64_opt | production | 0 | 0 | 0 | 0 | 0 / 0 |
| arm64_opt | packed_single | 1,336,502 | 293,298 | 331,256 | 33,912 | 1 / 1 |
| arm64_opt | packed_double | 1,952 | 98,320 | 50,544 | 8 | 1 / 1 |
| arm64_strict | production | 0 | 0 | 0 | 0 | 0 / 0 |
| arm64_strict | packed_single | 1,339,781 | 294,827 | 331,256 | 33,777 | 1 / 1 |
| arm64_strict | packed_double | 1,952 | 98,320 | 50,544 | 8 | 1 / 1 |
| x64_opt | production | 0 | 0 | 0 | 0 | 0 / 2 |
| x64_opt | packed_single | 1,312,290 | 294,233 | 252,404 | 33,928 | 1 / 2 |
| x64_opt | packed_double | 382,278 | 101,280 | 181,824 | 20 | 1 / 2 |
| x64_strict | production | 0 | 0 | 0 | 0 | 0 / 2 |
| x64_strict | packed_single | 1,312,290 | 294,233 | 252,404 | 33,928 | 1 / 2 |
| x64_strict | packed_double | 382,278 | 101,280 | 181,824 | 20 | 1 / 2 |

Native ARM64 reference controls deliver distinguishable Windows invalid (`0xc0000090`), overflow (`0xc0000091`) and divide-by-zero (`0xc000008e`) traps. Both mixed invalid/overflow lane orders are exercised. Production trap qualification exits **0**. The single candidate differs in 3 of 5 category observations; the double candidate differs in 4 of 5, so both trap gates exit **1**. Exact reference categories are required before a mixed-priority comparison can be admitted; unrelated SEH faults do not qualify.

The x64 emulator delivers sticky flags and no enabled traps for these controls. All x64 trap invocations exit **2** (`unavailable`), even though production masked checks pass. No x64 trap-priority claim is made. The runner propagates requested trap unavailability as **2**, and never converts that state to admission. Native ARM64 aggregate runner exits are **1** because the candidates are rejected; x64 runner exits are **2** because requested trap qualification is unavailable in addition to the masked candidate rejections.

## Concrete rejection evidence

- Native ARM64 nearest, positive finite: `x=0x3d2dc666`, `y=0xc13fc46a` gives powf bits `0x5ac79238`, packed-double bits `0x5ac79239`.
- Native ARM64 nearest, sRGB power: `x=0x3d32b0b3`, `y=0x3ed55555` gives powf bits `0x3e8ad577`, packed-double bits `0x3e8ad576`.
- Native ARM64 downward and toward-zero, sRGB transition `x=0x3eb048af`: the preserved quantizer produces channel **158**, while packed-double produces **159**. Every lane is checked. Both ordinary `opt` and strict profiles retain this difference with the frozen consumer constants.
- Exceptional `+0` with exponent bits `0x80000001`: powf raises divide-by-zero (flag **8**), while the masked packed-double result reports no flags.
- Exceptional `+0` with exponent `-0`: powf preserves the `E2BIG` errno sentinel (**7**); the provisional packed-double side-effect handling writes `ERANGE` (**34**).
- Native ARM64 isolated negative-base fractional exponent: powf produces the invalid trap, while packed-double returns without that trap. A mixed case whose preserved expression first traps overflow instead traps invalid in packed-double.
- Windows x64 nearest positive finite `x=0x7ce60532`, `y=0xbf9bc5b8`: powf produces bits `0x00000000`, packed-double produces `0x00000001`. These are observations of the x64 CRT under ARM64 emulation, not evidence about every x64 libm provider.

The source logs retain more counterexamples and all per-mode/category counts. Differences in the numerical core already reject promotion; repairing only special-case masks or accepting unchanged 8-bit output under nearest rounding would not satisfy the contract.

## Local CRT and compiler provider assessment

The installed Windows SDK UCRT source inventory contains **511 files**, and the selected MSVC 14.44.35207 CRT source inventory contains **494 files**. Complete source searches find no `powf` or `pow` function definition or call in either source tree; `rg` returns **1** for no matches. The LLVM 23 resource inventory contains **332 files**. CPU headers expose scalar CRT calls; the other power declarations found belong to GPU/device wrappers or the already inspected elementwise builtin. The complete inventories, source searches and actual target-specific header search commands are saved under `__cmake/Testing/task4_pow_provider`.

The MSVC `immintrin.h` header declares `_mm_pow_ps`, which initially looked like a possible packed provider. A standalone probe derived from the real x64 `opt` compile/link commands, with an explicit declaration matching that header, compiles with exit **0**, errors **0**, warnings **0**. Linking exits **1** because `_mm_pow_ps` is undefined. The complete linker log contains two expected error lines: the unresolved provider and Clang's resulting link-failed summary. This is provider-availability evidence, not a production build failure. Inventories of the installed library roots find no SVML, SLEEF, libmvec or equivalent vector-math library. `llvm-nm --defined-only --extern-only` completes for both architectures' UCRT import/static libraries with exit **0** and **0 diagnostics**; no vector power entry point is defined there. The header declaration therefore does not supply a callable packed implementation in this toolchain/profile.

The static UCRT archives do contain scalar `powf` machine-code implementations. Exact full-name archive-member extraction and disassembly both exit **0** for ARM64 and x64. ARM64 archive paths identify ARM optimized-routines code: the inspected routine uses 16 log-reduction table entries, 32 exp2 table entries, scalar binary64 FMA steps, an explicit nearest-away integer reduction, subnormal normalization and separate invalid/divide-by-zero/overflow/underflow helpers. The x64 routine has runtime SSE2/FMA3 dispatch, different near-one and general-log paths, private inverse/log/exp tables, NaN-payload branches, and `_powf_special` calls; its older-ISA integer classification temporarily changes and restores MXCSR. These are concrete provider differences, so substituting one mathematically accurate polynomial cannot establish identical behavior for both.

At that SDK-only checkpoint, a faithful qualified packed implementation was unavailable in the local sources or installed CPU vector libraries. The later actual-OS provider investigation and ARM implementation above resolve that narrower native-profile avenue. Such an adaptation would still need the selected provider's exact constants and operation order, its runtime ISA selection, directed-rounding and subnormal behavior, and the reference expression's observed side-effect order. It would also have to avoid evaluating inactive exceptional lanes and reproduce the first enabled trap category and final `errno` across all lanes. Static SDK archive disassembly is not assumed identical to the dynamically loaded OS CRT. The existing exact gate must decide admission; matching only the finite core or nearest-mode sRGB bytes would leave observable failures. At that earlier checkpoint, no reverse-engineered or unqualified replacement had been added to stable sources, and no dependencies or services were provisioned. `provider_assessment.json` records the commands, exits, complete log classification and these scope limits.

## Commands and complete-log checks

From the repository root, these four commands each completed; individual compiler, linker, masked qualifier and trap exit codes are recorded in `results.json`:

```powershell
python -B tests/performance/simd_optimization/run_pow_qualification.py --build-dir __cmake/build/windows-clang-arm64 --output __cmake/Testing/task4_pow_after_alignment_arm64_opt --traps
python -B tests/performance/simd_optimization/run_pow_qualification.py --build-dir __cmake/build/windows-clang-arm64 --output __cmake/Testing/task4_pow_after_alignment_arm64_strict --strict --traps
python -B tests/performance/simd_optimization/run_pow_qualification.py --build-dir __cmake/build/windows-clang-x64 --output __cmake/Testing/task4_pow_after_alignment_x64_opt --traps
python -B tests/performance/simd_optimization/run_pow_qualification.py --build-dir __cmake/build/windows-clang-x64 --output __cmake/Testing/task4_pow_after_alignment_x64_strict --strict --traps
```

`python -B tests/performance/simd_optimization/run_pow_qualification.py --help` exited **0**. Python AST parsing, syntax compilation and import exited **0**. Importing the runner and reparsing every saved qualifier log reproduced all 16 stored category/mode rows for each of the 12 profile/variant records. No test suite was added merely to mirror the implementation.

After numerical execution and the six existing benchmark checks, the runner source was normalized from LF to CRLF to match the repository's `text`/`eol=crlf` checkout attributes. The numerical execution-time runner hash remains `53dd37f33dca2d7a1b0afea6bfcf68ed8d1d73cb3bd9c22f93c5e1df8d47b1d9`; the current CRLF source hash is `f94d3c85bd0dbebdd28fd84e523ba8ee7eda6b72ead376f896367f076b7c012b`. The preserved LF source and original runner-validation records remain in `runner_execution_time_lf_*` artifacts. Normalized decoded text and AST are identical. Current-source `--help`, syntax compilation and import all exit **0**, and reparsing the saved logs reproduces every one of the **192** stored rows. The complete help log has one expected exception-token match in the `--traps` option description and **0** unexpected diagnostics. `runner_newline_normalization.json` and `runner_post_format_log_scan.json` distinguish the two byte snapshots. No numerical build or profile was rerun for this newline-only change, and earlier execution-time hashes were not relabelled as current-source execution.

The complete eight compile/link logs have **0** error/warning/exception hits. All 24 masked/trap runtime logs were scanned in full. Their **424** error/warning/exception-token hits consist of expected `exceptional` corpus identifiers and explicit SEH observations; **0** remain unclassified or unexpected. Candidate comparison differences are deliberate rejection evidence and remain failures of those experiments, not repaired production behavior. Final standalone log classifications are saved in `full_log_scan_final.json`.

The canonical opt-in CMake target registration was configured and built before the later shared Expected-alignment repair, after the shared-tree workers granted a safe window:

```powershell
cmake --preset windows-clang-arm64 -DNWB_BUILD_SIMD_BENCHMARKS=ON
cmake --build --preset windows-clang-arm64-opt --target nwb_simd_pow_qualification --parallel 4
```

Both commands exited **0**, with **0** diagnostics in their full logs. Only the qualifier object/executable were built; no production or smoke binary/asset was changed. All three canonical variants and their trap paths were executed. Production returned **0/0** (masked/traps), both candidates **1/1**, and every per-mode observation count exactly matched the standalone ARM64 `opt` evidence. `canonical_arm64_results.json` records these commands, the target's compile entry, all exits and comparison results. Across the eight standalone compile/link logs plus the two canonical configure/build logs, **0** diagnostic hits remain. Across the 24 standalone logs and six earlier canonical-registration runtime logs, **530** expected corpus-identifier/SEH hits are classified and **0** unexpected hits remain.

The four standalone profiles were freshly recompiled and rerun after the complete Expected-alignment repair, including the aligned vector results, all affected existing math/domain result records, and this qualifier's `Observation` record. They use fresh `task4_pow_after_alignment_*` directories. All four compile/link pairs exit **0**, all production controls remain zero, and every candidate per-mode/category count exactly matches the earlier snapshot. The final eight standalone compile/link logs contain **0** diagnostics; all final 24 runtime logs contain **424** classified expected corpus/SEH hits and **0** unexpected hits. `all_profiles_final.json`, `full_log_scan_final.json` and `final_header_inputs.json` preserve this final qualification and all included math-header hashes. Earlier canonical-target registration evidence is retained separately rather than relabelled as a new build.

The related alignment investigation enumerated the first-party Expected/native-SIMD-record intersection and checked every direct candidate in both real dbg compilation profiles. The Windows STL packing reduced alignment for naturally aligned native-vector payloads and some containing records. Fifteen existing aggregates received explicit native alignment and Expected guards; their APIs and fields remain unchanged. Paired AST layouts establish **38 payload/profile comparisons with zero changes to payload size, native alignment or field offsets**, and zero insufficient Expected alignments after repair. For example, x64 `MatrixDoubleDecomposition` remains 96 bytes/alignment 32 while its Expected wrapper grows from 104/8 to 128/32; ARM64 `CsgShapeBounds` remains 48/16 while its Expected wrapper grows from 56/8 to 64/16. No serialized payload or schema changed. `task4_expected_simd_record_layout_changes.json` retains all before/after values. Eight repeated direct-record/matrix compilation probes exit **0**, errors **0**, warnings **0**. The additional broad candidate dispositions and native-field/union/transitive controls found no further failures.

## Final existing benchmark correctness qualifiers

After the complete alignment repair, the build owner completed the canonical Windows ARM64 `opt` all-target build with exit **0**, errors **0** and warnings **0**. Its full log has two broad exception-token matches, both source filenames for packet-runtime exception finalizers rather than diagnostics. The final executable manifest identifies all six existing benchmark images. Each image's SHA-256 matched that manifest before execution and remained unchanged afterward.

All six README correctness commands ran to completion, serially under the same allowed logical-CPU affinity (mask `0x1`). No timing command, configure command, build command or generated-tree mutation was performed by this qualification step.

| Final `__exec/windows/arm64/full/opt` executable and arguments | Exit | Reported result |
| --- | ---: | --- |
| `simd_vector_benchmark.exe --verify` | 0 | `failures=0`, `reduction_cases=262144`, `factory_cases=180`, `axis_cases=112` |
| `simd_replicated_benchmark.exe --verify` | 0 | `failures=0`, `vector_cases=1376256` |
| `simd_geometry_benchmark.exe validate` | 0 | 400,000 exact containment probes and applicable winding/FENV checks passed. |
| `simd_memory_storage_benchmark.exe --qualify` | 0 | Applicable exact-byte/tail, adjacent-range, admission, integer-bit and FENV checks passed. |
| `simd_mask_benchmark.exe --verify` | 0 | `qualification_status=0` |
| `simd_box_normal_benchmark.exe verify` | 0 | `single=0`, `double=0`, `cases=262144` |

The complete six stdout/stderr streams have **0 error/warning/exception-token hits**, **0 unexpected diagnostics**, and **0 failing exits**. `__cmake/Testing/task4_simd_final_qualifiers/results.json` preserves every command, working directory, actual `opt` compile entry, source/image hashes, affinity, full-log counts and individual exit. `targets_enumerated_before_run.json` lists all six targets before execution; `final_opt_build_gate.json` classifies the complete retained build log.

These Windows runs do not execute the existing geometry probe's Linux-only enabled-trap subprocess branch or the memory probe's Linux-only protected-page checks. The geometry Windows branch repeats masked exception equivalence instead. Their generic success banners are interpreted only within those actual compiled guards; the README now explains this current platform boundary. The new power harness's Windows SEH checks are separate evidence and do not establish these other unavailable branches. No new provider was configured and no cross-platform verification is inferred from the six passing commands.

## Evidence and limits

The root evidence directory is `__cmake/Testing/task4_pow_qualification`; it contains the requirements/rule inventory, host details, aggregate `all_profiles_final.json`, Python validation and complete-log classifications. The four final profile directories named in the commands contain full original/derived compile commands, compiler version, source hash, revision, compile/link logs, complete masked/trap logs, per-mode counts and disassembly. The standalone source was reviewed read-only after the final code changes; the trap distinguishability and runner-admission findings were corrected and the final review reported no remaining findings.

Linux execution is unavailable in this environment; no Linux numerical or enabled-trap qualification was performed. Native x64 hardware was not available. These earlier runs establish rejection and alignment behavior, not the later implementation or its timing. The current native ARM64 implementation and its remaining scalar/backend limits are described above; the whole replacement obligation remains partial.
