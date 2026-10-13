# SIMD optimization qualification history

This report preserves recorded implementation reviews, numerical qualification, and measurements. Its results apply to the listed compiler, CPU, profiles, and inputs. Current benchmark operation and measurement procedures are documented in [README.md](README.md).

## Native review: 2026-10-13

The 2026-10-13 review used Clang 23.1.3, normal engine `opt` flags, and a Snapdragon X2 Elite Extreme X2E94100 Oryon CPU pinned to logical CPU 8. Mask and box-normal candidates were first qualified against frozen formulas, then measured with identical inputs and compiler settings. The accepted box-normal length reuse is restricted to NEON64. Quaternion Exp and rotation-normal now also use the existing replicated SinCos helper on NEON64. Each general wrapper reduces eight libm call sites to two; in the measured finite loops, Clang already eliminates unused results, so the hot path changes from four calls to two. The timed loop retains its per-iteration angle load, libm work and observed accumulator. The corresponding baseline SSE2 Single experiment changed exception flags because Clang emitted packed additions in discarded lanes, so that path retains its original formula. A packed glyph-extent experiment was also rejected because it removed existing inexact exceptions in discarded lanes. No x64 hardware speed claim follows from emulated x64 qualification.

Box-normal exception probes on native Windows ARM64 delivered real floating-point traps, but the OS reported the same generic exception code for invalid and overflow. They establish trap/no-trap equivalence for those fixtures; masked flag checks and preservation of arithmetic order supply the additional numerical evidence. The x64 emulator did not deliver enabled floating-point traps, including explicit controls, so its qualification establishes result bits, flags and `errno` rather than mixed-category trap priority.

## Final native measurements (2026-10-13)

Fresh CMake `opt` binaries were measured serially on logical CPU 8 of the Snapdragon X2 Elite Extreme X2E94100 with Clang 23.1.3 and the engine's normal `-O2` settings.
Each row uses warm-ups and seven alternating reference/candidate pairs with matching checksums.
Ratios are candidate time divided by reference time; values below one indicate less time.

| Workload | Throughput ratio | Dependent latency ratio |
| --- | ---: | ---: |
| Binary64 sign-mask extraction | 0.566 | 0.955 |
| Float4 equality predicate | 1.002 | 0.964 |
| Double4 equality predicate | 0.640 | 0.904 |
| Double3 equality predicate | 0.817 | 1.000 |
| Single box normal | 0.938 | 0.995 |
| Double box normal | 0.951 | 1.001 |
| Double quaternion rotation-normal | 0.452 | — |
| Double quaternion exponential | 0.442 | — |

Mask samples initially used 4,000,000 iterations, box samples 1,000,000, rotation-normal samples 2,000,000, and exponential samples 500,000 (four operations per loop).
Two isolated scheduling outliers in Double4 latency and Double box throughput prompted longer seven-pair repetitions at 20,000,000 and 5,000,000 iterations respectively; the table uses those follow-up medians.
Float4 throughput was also repeated at 20,000,000 iterations and remained effectively unchanged.
Both initial and follow-up raw samples are retained under `__cmake/Testing/simd_optimization/final_native_20261013`, together with compile commands, final COFF disassembly and source/object hashes.

The final public APIs passed masked result-bit, flag and `errno` qualification on ARM64, ARM64EC, SSE2, SSE4.1, AVX, AVX with FMA and AVX2 with FMA.
Native enabled-exception quaternion probes preserve the tested first exception, including inexact before later invalid arithmetic.
The focused partial-mask regression also compares the preserved original formula at `O0` and `O2`; discarded-W exception behavior varies with compiler optimization and ISA, and both implementations match in those contexts.
These measurements describe the listed CPU kernels and inputs; they do not establish frame-rate gains or x64 hardware performance.
