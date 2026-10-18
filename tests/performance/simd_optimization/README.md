# SIMD optimization benchmarks

These opt-in CPU benchmarks compare preserved reference formulas, current production helpers, and experimental implementations. They qualify numerical behavior before measuring speed. A successful experimental measurement does not mean that implementation is part of the production API.

## Build and qualify

Run the following from the repository root on Linux x64, with the normal project dependencies installed. The existing preset supplies the engine's compiler, language, include, and ISA settings.

```bash
set -euo pipefail
cmake --preset linux-clang-x64 -DNWB_BUILD_SIMD_BENCHMARKS=ON
cmake --build --preset linux-clang-opt --target nwb_math_tests nwb_simd_vector_benchmark nwb_simd_geometry_benchmark nwb_simd_memory_storage_benchmark nwb_simd_replicated_benchmark \
    nwb_simd_mask_benchmark nwb_simd_box_normal_benchmark nwb_simd_pow_qualification

simd_cpu=4
simd_bin_dir=__exec/linux/x64/full/opt
simd_result_dir=__cmake/Testing/simd_optimization/review_run
mkdir -p "$simd_result_dir"

taskset -c "$simd_cpu" ctest --test-dir __cmake/build/linux-clang-x64 -C opt --parallel 1 -R '^nwb_math_tests$' --output-on-failure --output-log "$PWD/$simd_result_dir/math_opt.log"
taskset -c "$simd_cpu" "$simd_bin_dir/simd_vector_benchmark" --verify > "$simd_result_dir/vector_qualify.log" 2>&1
taskset -c "$simd_cpu" "$simd_bin_dir/simd_replicated_benchmark" --verify > "$simd_result_dir/replicated_qualify.log" 2>&1
taskset -c "$simd_cpu" "$simd_bin_dir/simd_geometry_benchmark" validate > "$simd_result_dir/geometry_qualify.log" 2>&1
taskset -c "$simd_cpu" "$simd_bin_dir/simd_memory_storage_benchmark" --qualify > "$simd_result_dir/memory_qualify.log" 2>&1
taskset -c "$simd_cpu" "$simd_bin_dir/simd_mask_benchmark" --verify > "$simd_result_dir/mask_qualify.log" 2>&1
taskset -c "$simd_cpu" "$simd_bin_dir/simd_box_normal_benchmark" verify > "$simd_result_dir/box_normal_qualify.log" 2>&1
taskset -c "$simd_cpu" "$simd_bin_dir/simd_pow_qualification" production > "$simd_result_dir/pow_production_qualify.log" 2>&1
```

Choose an allowed logical CPU and keep it fixed for the whole comparison. The x64 preset enables AVX2, FMA, F16C and SSE4.2; run its binaries on a CPU supporting those features. Benchmarks remain disabled when `NWB_BUILD_SIMD_BENCHMARKS` is omitted or `OFF`.

The math suite covers ordered cancellation, distinct NaN payloads, inactive signaling lanes, rounding modes, and packed Dot3 exception priority. Vector verification compares result bits, floating-point flags and `errno` over exceptional and random inputs in all four standard rounding modes. Geometry validation checks containment, winding, degenerate hulls, inactive W, nonfinite/overflow cases and enabled exception categories. Memory qualification checks exact byte tails, adjacent ranges, alignment/capacity admission, every integer bit, and preserved floating-point flags; Linux also runs protected-page checks.

## Reference and candidate meanings

| Executable | Reference | Compared implementation |
| --- | --- | --- |
| `simd_vector_benchmark` | Preserved dot/sum and scalar-angle factory formulas | `candidate` calls current production APIs. |
| `simd_replicated_benchmark` | Preserved composition with ordinary four-lane primitives | `candidate` calls current public consumers; native-length and controlled-length prototypes remain local experiments. |
| `simd_geometry_benchmark` | Depends on the selected mode below | Default compares the public production API; isolated guard/absolute-value experiments are validated against it. |
| `simd_memory_storage_benchmark` | Current `SIMDMemoryCopy` and storage equality helpers | Local AVX copy and wide integer equality experiments. |
| `simd_mask_benchmark` | Frozen Double sign extraction and comparison-mask reductions | Current public mask and predicate APIs. |
| `simd_box_normal_benchmark` | Frozen Single/Double box-normal formulas with two length reductions | Current public box-normal APIs. |
| `simd_pow_qualification` | Frozen four-lane calls to scalar `Pow(float, float)` | Public `VectorPow` or local packed power experiments. |

Keep copied reference formulas unchanged when promoting a candidate. They still share lower-level production primitives: a later change to one of those primitives also needs a preserved reference for that operation. Rebuild and requalify after each source change. Reusing an old binary or relabeling an experimental result as a production measurement invalidates the comparison.

## Vector reductions and factories

```text
simd_vector_benchmark --verify
simd_vector_benchmark --measure OPERATION baseline|candidate ITERATIONS throughput|latency
```

Operations are `dot2`, `dot3`, `dot4`, `sum`, `rotation_x`, `perspective_fov`, and `quaternion_normal`. Use both throughput and latency for reductions; factories accept throughput only and reject latency requests. Each measurement writes one JSON record with elapsed time, operation count and checksum.

The supplied runner warms both variants, runs one process at a time pinned to the selected CPU, alternates baseline/candidate order, checks paired checksums, and saves all samples as JSONL beside a JSON median summary. Its default profiles use `O3`; select the engine `O2` profile explicitly for `opt` conclusions. The copy below gives the freshly built CMake binary the runner's expected profile filename.

```bash
cp "$simd_bin_dir/simd_vector_benchmark" "$simd_result_dir/vector_benchmark_engine_o2"
python3 tests/performance/simd_optimization/run_vector_benchmark.py --root "$simd_result_dir" --profiles engine_o2 --cpu "$simd_cpu" --samples 7 --output "$simd_result_dir/vector_factories_and_dot2.json"
python3 tests/performance/simd_optimization/run_vector_benchmark.py --root "$simd_result_dir" --profiles engine_o2 --cpu "$simd_cpu" --samples 7 --wide --output "$simd_result_dir/vector_dot3_dot4_sum.json"
```

The first command set measures Dot2 and the factories; `--wide` measures Dot3, Dot4 and Sum with both reduction workloads. Runner `speedup` is baseline time divided by candidate time, so values above one indicate faster candidates.

## Replicated primitive experiments

```text
simd_replicated_benchmark --verify
simd_replicated_benchmark --measure OPERATION baseline|candidate ITERATIONS throughput
```

Operations are `ln`, `exp`, `normals2`, `normals3`, `normals4`, `vectors2`, `vectors3`, `vectors4`, `slerp`, `length2`, `length3`, `length4`, `reciprocal2`, `reciprocal3`, `reciprocal4`, `normalize2`, `normalize3`, `normalize4`, `clamp2`, `clamp3`, and `clamp4`. This probe accepts throughput only. `candidate` selects the current public API for Ln, Exp, normal/vector angles, Slerp and reciprocal lengths; `length2`/`length3`/`length4` select local scalar-sqrt experiments. The `normalize*` and `clamp*` cases select a local shared-helper experiment, substituting scalar reciprocal sqrt only on non-AVX SSE2 profiles. Verification compares production results, flags and `errno` against the preserved formulas as well as checking the local prototypes.

Production profile guards remain active inside those public calls. Replicated Ln/angle/Slerp primitives apply across the supported math backends; quaternion Exp and rotation-normal SinCos use the replicated helper on AVX2 and NEON64. Reciprocal lengths in 2D/3D use native scalar sqrt/divide on x64 SSE2 and newer; 4D uses that path only on non-AVX SSE2/SSE4. Perspective FOV uses replicated SinCos on all qualified profiles. Plain length, `NormalizeOr` and clamping APIs retain their existing implementation. Weighted Slerp sine arguments remain nonuniform and retain the ordinary vector path.

The following driver runs warmed, alternating pairs serially and rejects checksum differences. Retain the raw records before calculating summaries.

```bash
python3 - "$simd_bin_dir/simd_replicated_benchmark" "$simd_cpu" "$simd_result_dir/replicated_pairs.jsonl" <<'PY'
import json
import subprocess
import sys

binary, cpu, output = sys.argv[1:]
operations = ("ln", "exp", "normals2", "normals3", "normals4", "vectors2", "vectors3", "vectors4", "slerp", "length2", "length3", "length4", "reciprocal2", "reciprocal3", "reciprocal4", "normalize2", "normalize3", "normalize4", "clamp2", "clamp3", "clamp4")

def measure(operation, variant, iterations):
    command = ["taskset", "-c", cpu, binary, "--measure", operation, variant, str(iterations), "throughput"]
    completed = subprocess.run(command, check=True, capture_output=True, text=True)
    return json.loads(completed.stdout)

with open(output, "w", encoding="utf-8") as raw:
    for operation in operations:
        for variant in ("baseline", "candidate"):
            measure(operation, variant, 10000)
        for sample in range(7):
            order = ("baseline", "candidate") if sample % 2 == 0 else ("candidate", "baseline")
            pair = {}
            for variant in order:
                record = measure(operation, variant, 500000)
                record.update(cpu=int(cpu), profile="engine_o2", sample=sample)
                raw.write(json.dumps(record) + "\n")
                raw.flush()
                pair[variant] = record
            if pair["baseline"]["checksum"] != pair["candidate"]["checksum"]:
                raise RuntimeError(f"checksum mismatch: {operation}, sample {sample}")
PY
```

## Geometry modes

| Mode | Timed pair |
| --- | --- |
| `validate` | Qualification only. |
| No argument | Frozen original intersection implementation versus public `TriangleDoubleTests::Intersects`, including the selected zero-axis guard and AVX2 absolute-value caching. |
| `guard` | Current Dot3 without the guard versus current Dot3 with the guard; isolates guard cost. |
| `abs` | Guarded implementation versus experimental hoisting of absolute coordinate/roundoff values. |

Every mode qualifies first. Timing modes run seven rounds with alternating order across coplanar, point, segment, noncoplanar hit/miss, AABB miss and point/point fixtures. They check the expected intersection count and preserve a checksum in each `sample` row. Default rows use `baseline`/`production`; `guard` uses `current_dot3_baseline`/`zero_axis`, and `abs` uses `zero_axis`/`abs_hoisted`. Save the complete output, including raw rows and median/minimum/maximum summaries.

```bash
taskset -c "$simd_cpu" "$simd_bin_dir/simd_geometry_benchmark" > "$simd_result_dir/geometry_production.csv" 2> "$simd_result_dir/geometry_production.stderr"
taskset -c "$simd_cpu" "$simd_bin_dir/simd_geometry_benchmark" guard > "$simd_result_dir/geometry_guard.csv" 2> "$simd_result_dir/geometry_guard.stderr"
taskset -c "$simd_cpu" "$simd_bin_dir/simd_geometry_benchmark" abs > "$simd_result_dir/geometry_abs.csv" 2> "$simd_result_dir/geometry_abs.stderr"
```

Geometry ratio summaries are selected implementation time divided by reference time, so values below one indicate faster selected implementations. The default mode compares the public API with the frozen reference; `guard` and `abs` isolate local experiments.

## Memory and storage modes

```text
simd_memory_storage_benchmark --qualify
simd_memory_storage_benchmark --bench-copy [SAMPLES]
simd_memory_storage_benchmark --bench-equality [SAMPLES]
simd_memory_storage_benchmark --bench-equality-first-word [SAMPLES]
```

Samples default to seven and accept 1 through 100. Each measurement invocation qualifies first, warms both variants, and alternates their order internally. Copy cases cover fixed/dynamic sizes, 16/32-byte offsets, a reused warm buffer, and a larger streaming working set. Equality cases cover aligned/unaligned offsets and equal/early/late mismatches; the first-word experiment covers each mismatching word separately. Here `streaming` describes the working set, not a non-temporal store policy.

```bash
taskset -c "$simd_cpu" "$simd_bin_dir/simd_memory_storage_benchmark" --bench-copy 7 > "$simd_result_dir/memory_copy.csv" 2> "$simd_result_dir/memory_copy.stderr"
taskset -c "$simd_cpu" "$simd_bin_dir/simd_memory_storage_benchmark" --bench-equality 7 > "$simd_result_dir/storage_wide.csv" 2> "$simd_result_dir/storage_wide.stderr"
taskset -c "$simd_cpu" "$simd_bin_dir/simd_memory_storage_benchmark" --bench-equality-first-word 7 > "$simd_result_dir/storage_first_word.csv" 2> "$simd_result_dir/storage_first_word.stderr"
```

CSV `candidate_over_baseline` is candidate time divided by baseline time. Outputs are consumed through an internal volatile checksum, and equality warm-up checks expected counts; the current CSV does not emit a checksum for each timed pair. Preserve qualification logs and inspect generated code rather than treating CSV timing ratios alone as correctness evidence.

## Measurement scope and rejection criteria

Run all timed work serially, including benchmarks on other cores, builds and qualification jobs. Keep CPU affinity, compiler, ISA, optimization level, floating-point control state, iteration count, inputs and working-set size matched. Save the revision, working diff, compiler version, CPU model/affinity, full compile commands and disassembly with the raw results under `__cmake/Testing`; use a fresh result directory for each build.

The CMake `opt` build uses `-O2`, `NDEBUG`, `NWB_OPTIMIZE`, and the engine's normal code-generation settings. `O3` probes and strict floating-point profiles are additional qualification evidence, not substitutes for matching `opt`. For standalone SSE2, SSE4.1, AVX or AVX2 profiles, derive commands from the actual `opt` entry in `compile_commands.json`, change only the intended ISA switches, and record those commands. Requalify each profile. Linux x64 results do not establish performance on Windows, ARM64, other CPUs, or unmeasured cache/input distributions.

Reject a candidate if result bits or required booleans differ, paired checksums differ, flags/`errno`/first enabled exception categories differ, inactive signaling lanes suppressed by the qualified original path are evaluated, exact bounds/admission checks fail, or emitted code removes intended work. Reject changes with repeatable material regressions in relevant latency, nondegenerate geometry, early-mismatch or cache/offset controls. Repeat an inconclusive measurement when run-order or system variation is comparable to the apparent gain. Preserve sequential reductions and packed active-lane exception priority; a smaller instruction count alone does not establish an improvement.

## Native mask and box-normal modes

```text
simd_mask_benchmark --verify
simd_mask_benchmark --measure move|equal_float|equal_double|equal_double3 baseline|candidate ITERATIONS throughput|latency
simd_box_normal_benchmark verify
simd_box_normal_benchmark measure single|double original|candidate throughput|latency ITERATIONS
```

Mask verification includes arbitrary binary64 sign bits, canonical comparison masks, exceptional floating inputs, inactive W and directed rounding. Comparison predicates preserve the original comparison expression before reducing integer masks. Partial-predicate flag checks use the frozen baseline because compilers can omit discarded W comparisons; optimized ARM64 self-comparisons and x64/debug builds differ in whether an inactive signaling W raises invalid. Box-normal verification compares result bits, flags and `errno` in all four rounding modes, including overflow, subnormal and nonfinite inputs. Both benchmark families consume checksums and provide independent throughput and dependent latency workloads.

On Windows ARM64, configure `windows-clang-arm64` with `NWB_BUILD_SIMD_BENCHMARKS=ON`, then build these targets with `--config opt` in `__cmake/build/windows-clang-arm64`. Executables are under `__exec/windows/arm64/full/opt` and have the `.exe` suffix. Pin the orchestrating process before launching children, for example with `[System.Diagnostics.Process]::GetCurrentProcess().ProcessorAffinity = [IntPtr]256` in PowerShell when logical CPU 8 is allowed. Run warm-ups followed by at least seven alternating reference/candidate pairs serially; check paired checksums before calculating candidate/reference time ratios.

On Windows, the existing geometry probe checks masked exception flags; its enabled-trap subprocess checks and the memory probe's protected-page checks are Linux-only. Their generic success banners do not establish those unavailable branches. The separate power qualifier implements its own Windows SEH controls.

Box-normal length reuse is restricted to NEON64; other profiles retain their original arithmetic order and reductions. Exception verification must distinguish masked flags, trap/no-trap behavior, and the first enabled exception category. A host or emulator that does not deliver distinguishable enabled traps cannot establish mixed-category trap priority.

Recorded native measurements and review evidence are retained in the separate [qualification report](qualification_history.md).

## Packed single-precision power qualification

Public `VectorPow` uses a packed table/FMA implementation with the release CRT on native Windows ARM64, excluding ARM64EC and `_DEBUG` CRT linkage. Its four lanes reduce the logarithm and exponential in binary64 SIMD pairs and narrow once to binary32. Packed integer admission selects a bounded ordinary path when x lies in [2^-16, 2^16] and |y| <= 4, with neutralized exact zero/one shortcuts; other inputs use the generic packed path. Loads, table indices and lane-control branches do not move the numerical intermediates out of the SIMD domain.

The implementation preserves the measured Windows CRT operation order and special-value side effects, including normalization before the subnormal-base y == 1 return. Enabled standard floating-point exceptions use lane-ordered packed primitives with per-operation state restoration and exception replay. Exact tiny arithmetic results can trap underflow without setting a masked sticky flag; narrowing checks tininess before rounding and inexact, while excluding binary64 subnormals consumed as zero by flush mode. Native ARM input-denormal exceptions select the original four scalar calls because the available packed instruction delivers a different exception category. Debug CRT linkage (`_DEBUG`) retains the original scalar expression under its unoptimized inline policy. Other architectures and platforms retain the original scalar implementation. These are explicit remaining scalar paths, not a universal powf replacement. Provider-specific behavior must be requalified on a different CRT or execution environment.

The default math edge tests compare the public API with the frozen formula across directed rounding, mixed special values, adjacent sRGB boundaries, seeded flags/errno, and native Windows ARM64 denormal modes/status. The opt-in corpus below provides broader numerical qualification; neither finite corpus admission nor local CPU timing proves equivalence or speed on every supported provider. The separate [power qualification report](pow_qualification_report.md) records the measured scope and limits.

`simd_pow_qualification production|packed_single|packed_double [traps]` compares the frozen scalar `powf` formula with the selected implementation. `production` uses public `VectorPow`; the other variants remain local experiments and are not production alternatives. The single-precision experiment composes packed `Log2` and `Exp2`. The binary64 experiment extracts binary32 mantissas and exponents, computes a reduced atanh logarithm series and exponential series in packed double lanes, narrows once, and applies integer masks for exceptional result shapes. Its masks and `errno` handling remain subject to the same admission checks as the numerical core.

Each variant runs all four standard rounding modes. Per mode it checks the Cartesian product of 29 exceptional bit patterns in each of four lane positions, 32,768 arbitrary four-lane pairs, 65,536 positive-finite four-lane pairs, every 8-bit sRGB decode input, the complete 16-bit uniform linear grid, and 33 adjacent binary32 inputs around each of the 255 rounded sRGB quantization transitions. The transition locations are found by binary search in the preserved consumer expression under the active rounding mode. Constants that are compile-time expressions in the consumers stay `constexpr` in the probe. This is a finite qualification corpus; it is not an exhaustive proof over all binary32 input pairs.

Output separates differing result lanes, floating-point flag observations, `errno` observations and quantized channels. Any difference makes the selected variant return `1`; a zero result admits only that measured corpus and profile. Check the independent `production` control before interpreting a candidate. Rejected experiments must stay outside production and are not registered as passing CTest cases. No speed claim is made by this qualification-only probe.

On Windows, `traps` separately checks isolated invalid, overflow and divide-by-zero controls before comparing mixed invalid/overflow lane orders. An additional edge gate checks exact-tiny `.5^149`/`.5^127` and subnormal-base zero-exponent behavior with underflow and underflow-plus-inexact enabled, all four rounding modes and available denormal modes. It compares delivered categories, errno, mapped/native status and control state; exact-tiny reference controls must deliver the expected underflow category. Reference controls must deliver the exact distinct Windows exception categories; unrelated SEH faults or a host/emulator delivering only sticky flags make trap qualification unavailable (`2`). A different candidate category returns `1`. Other platforms currently return `2` for `traps`; masked flag checks still run in the ordinary qualification. Do not infer first enabled exception priority from the masked checks.

For standalone Windows qualification, the runner uses an existing `nwb_math_tests` `opt` entry from `compile_commands.json`, preserving its target, ISA, includes, defines and code-generation flags while replacing the source and output. It removes the generated `CMAKE_INTDIR` definition and records the full original entry and resulting compile/link commands. It builds only this test-owned probe in the chosen evidence directory and never configures or mutates the generated build tree.

```powershell
python -B tests/performance/simd_optimization/run_pow_qualification.py --build-dir __cmake/build/windows-clang-arm64 --output __cmake/Testing/pow_arm64_opt --traps
python -B tests/performance/simd_optimization/run_pow_qualification.py --build-dir __cmake/build/windows-clang-arm64 --output __cmake/Testing/pow_arm64_strict --strict --traps
```

Use the matching x64 build directory to qualify that target. Strict FP is additional evidence and does not replace the actual `opt` profile. The runner saves full compiler/linker/variant logs and per-mode counts in `results.json`. It returns `1` when any selected variant is rejected, `2` for build/control failure or requested trap qualification unavailable on the host, and `0` only when every requested gate passes. It runs no timings, so its results establish neither speed nor behavior on other platforms. Recorded rejection evidence is retained in the separate [power qualification report](pow_qualification_report.md).
