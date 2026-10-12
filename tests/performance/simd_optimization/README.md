# SIMD optimization benchmarks

These opt-in CPU benchmarks compare preserved reference formulas, current production helpers, and experimental implementations. They qualify numerical behavior before measuring speed. A successful experimental measurement does not mean that implementation is part of the production API.

## Build and qualify

Run the following from the repository root on Linux x64, with the normal project dependencies installed. The existing preset supplies the engine's compiler, language, include, and ISA settings.

```bash
set -euo pipefail
cmake --preset linux-clang-x64 -DNWB_BUILD_SIMD_BENCHMARKS=ON
cmake --build --preset linux-clang-opt --target nwb_math_tests nwb_simd_vector_benchmark nwb_simd_geometry_benchmark nwb_simd_memory_storage_benchmark nwb_simd_replicated_benchmark

simd_cpu=4
simd_bin_dir=__exec/linux/x64/full/opt
simd_result_dir=__cmake/Testing/simd_optimization/review_run
mkdir -p "$simd_result_dir"

taskset -c "$simd_cpu" ctest --test-dir __cmake/build/linux-clang-x64 -C opt --parallel 1 -R '^nwb_math_tests$' --output-on-failure --output-log "$PWD/$simd_result_dir/math_opt.log"
taskset -c "$simd_cpu" "$simd_bin_dir/simd_vector_benchmark" --verify > "$simd_result_dir/vector_qualify.log" 2>&1
taskset -c "$simd_cpu" "$simd_bin_dir/simd_replicated_benchmark" --verify > "$simd_result_dir/replicated_qualify.log" 2>&1
taskset -c "$simd_cpu" "$simd_bin_dir/simd_geometry_benchmark" validate > "$simd_result_dir/geometry_qualify.log" 2>&1
taskset -c "$simd_cpu" "$simd_bin_dir/simd_memory_storage_benchmark" --qualify > "$simd_result_dir/memory_qualify.log" 2>&1
```

Choose an allowed logical CPU and keep it fixed for the whole comparison. The x64 preset enables AVX2, FMA, F16C and SSE4.2; run its binaries on a CPU supporting those features. Benchmarks remain disabled when `NWB_BUILD_SIMD_BENCHMARKS` is omitted or `OFF`.

The math suite covers ordered cancellation, distinct NaN payloads, inactive signaling lanes, rounding modes, and packed Dot3 exception priority. Vector verification compares result bits, floating-point flags and `errno` over exceptional and random inputs in all four standard rounding modes. Geometry validation checks containment, winding, degenerate hulls, inactive W, nonfinite/overflow cases and enabled exception categories. Memory qualification checks exact byte tails, adjacent ranges, alignment/capacity admission, every integer bit, and preserved floating-point flags; Linux also runs protected-page checks.

## Reference and candidate meanings

| Executable | Reference | Compared implementation |
| --- | --- | --- |
| `simd_vector_benchmark` | Preserved dot/sum and scalar-angle factory formulas | `candidate` calls current production APIs. |
| `simd_replicated_benchmark` | Preserved composition with ordinary four-lane primitives | `candidate` calls promoted public consumers; rejected native length and controlled-length prototypes remain local experiments. |
| `simd_geometry_benchmark` | Depends on the selected mode below | Default compares the public production API; isolated guard/absolute-value experiments are validated against it. |
| `simd_memory_storage_benchmark` | Current `SIMDMemoryCopy` and storage equality helpers | Local AVX copy and wide integer equality experiments. |

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

Operations are `ln`, `exp`, `normals2`, `normals3`, `normals4`, `vectors2`, `vectors3`, `vectors4`, `slerp`, `length2`, `length3`, `length4`, `reciprocal2`, `reciprocal3`, `reciprocal4`, `normalize2`, `normalize3`, `normalize4`, `clamp2`, `clamp3`, and `clamp4`. This probe accepts throughput only. `candidate` selects the current public API for Ln, Exp, normal/vector angles, Slerp and reciprocal lengths; `length2`/`length3`/`length4` retain rejected local scalar-sqrt experiments. The `normalize*` and `clamp*` cases retain a rejected shared-helper experiment, substituting scalar reciprocal sqrt only on non-AVX SSE2 profiles. Its isolated gains did not justify regressions across other dimensions/profiles. Verification compares production results, flags and `errno` against the preserved formulas as well as checking the local prototypes.

Production profile guards remain active inside those public calls. Replicated Ln/angle/Slerp primitives apply across the supported math backends; Exp and quaternion rotation-normal SinCos are AVX2-only. Reciprocal lengths in 2D/3D use native scalar sqrt/divide on x64 SSE2 and newer; 4D uses that path only on non-AVX SSE2/SSE4. Perspective FOV uses replicated SinCos on all qualified profiles. Plain length, `NormalizeOr` and clamping APIs retain their existing implementation. Weighted Slerp sine arguments remain nonuniform and retain the ordinary vector path.

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

Geometry ratio summaries are selected implementation time divided by reference time, so values below one indicate faster selected implementations. The default mode measures the combined production change; `guard` and `abs` isolate local experiments.

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

Reject a candidate if result bits or required booleans differ, paired checksums differ, flags/`errno`/first enabled exception categories differ, inactive signaling lanes are evaluated, exact bounds/admission checks fail, or emitted code removes intended work. Reject changes with repeatable material regressions in relevant latency, nondegenerate geometry, early-mismatch or cache/offset controls. Repeat an inconclusive measurement when run-order or system variation is comparable to the apparent gain. Preserve sequential reductions and packed active-lane exception priority; a smaller instruction count alone does not establish an improvement.
