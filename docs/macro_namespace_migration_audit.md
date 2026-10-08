# Macro and namespace ownership migration

Date: 2026-10-08. Source base: `b5181049e735e800cd2d845ea12d5b7f007df8a1`, pulled before the migration. Generated qualification evidence belongs under `__cmake/`; historical evidence and third-party source snapshots retain their original provenance.

## Source ownership

| Domain | Current contract |
| --- | --- |
| Shared utilities | NWBLot-owned macro APIs use `NWB_`, including compiler/platform, memory/string, stream, assertion, math, and global utility macros. Definitions remain in their owning headers; utility types and functions retain their existing scopes. |
| Build configuration | CMake defines `NWB_DEBUG`, `NWB_OPTIMIZE`, and `NWB_FINAL` directly for `dbg`, `opt`, and `fin`, without intermediate configuration aliases. |
| Interface declarations | Project C++ abstract interfaces retain the lowercase `interface` declaration annotation. `global/compile.h` supplies the portable `struct` expansion only when an external SDK has not already defined it. This canonical annotation is an intentional macro-prefix exception; external SDK spellings remain externally owned. |
| Engine namespace | Root `engine_namespace.h` alone defines `NWB_BEGIN` and `NWB_END`, opening and closing `namespace NWB`. It contains no engine type or service dependency. Engine-domain umbrellas include it and define their own domain wrappers. |
| Application namespace | `CoolStuff/Testbed/namespace.h` independently defines `TESTBED_BEGIN`/`TESTBED_END` for `namespace Testbed`, without including engine headers. Testbed headers use that local header; the owning CMake source list registers it. |
| Application types | The callback object is `Testbed::Project`; UI classes and sources live in `Testbed`, including `Testbed::UiSkinPreview` and the `Testbed::Ui*` galleries. Existing translation-unit helpers remain in their named `__hidden_*` groups inside the application namespace. |
| Loader adapter | The six required functions declared by `loader/project_entry.h` remain in `NWB`: client-size and title queries, startup configuration, callback creation, world creation, and world destruction. Testbed adapters delegate to their application-owned implementation or construct `Testbed::Project`. |

Engine consumers and applications use the engine API headers they need. Engine namespace definitions may arrive transitively through those headers; applications do not directly include `engine_namespace.h` to declare their own symbols. Global utilities do not acquire an engine namespace dependency.

Application-owned macro APIs use the application prefix, such as `TESTBED_`. Required external API, compiler interoperability, toolchain, and third-party names retain their documented spelling. Engine-owned shader feature and runtime macro APIs retain `NWB_` when consumed by application code. Existing `g_*` shader resource-view aliases, including `g_Control`, retain the current shader symbol contract; this utility/configuration naming migration does not rewrite shader resources or authored shader contracts. The former global-only prefix and root namespace-header path have no compatibility aliases or forwarding header.

Existing application-facing `NWB::Impl` feature APIs, including UI and renderer systems, remain available. Private/detail ownership and the existing selected graphics-provider boundary are preserved. Build include roots still expose the repository root; this change does not introduce an exported public-header architecture or filesystem-level include isolation.

Testbed's existing `ProjectTestbed` diagnostic strings remain stable for runtime harness markers. Those strings do not expose a C++ compatibility class or namespace alias.

## Cost and behavior

The macro migration changes identifier spelling, and the application namespace migration changes C++ symbol ownership. It adds no runtime dispatch, per-access decoding, owning-string conversion, per-resource state, or allocator machinery. No frame-time, CPU, RSS, or binary-size improvement is attributed to these naming changes. Required loader signatures and engine integration behavior remain the same.

## Documentation

`.helper/standard.md` defines the canonical macro prefix, independent application namespace, engine umbrella include ownership, and loader adapter boundary. `.helper/note.md` records the same rules. Current code examples, the root readme, and wiki Architecture, Project API, Code Reading Guide, Diagnostics and Invariants, and Diagnostics and Telemetry follow the current names. Wiki Build and Verification records the completed qualification. Historical qualification records remain historical.

## Verification

Qualification completed on Windows ARM64 with Clang 23.1.2 and Qualcomm Adreno X2-90. Evidence is retained under `__cmake/verification_macro_namespace_20261008/`. Other platform branches received the mechanical macro migration; these results do not establish native execution on those platforms.

### Builds and native suites

All three full builds passed, including their asset-cook work. Complete build logs contain no compiler warnings/errors, failed build markers, or asset-cook warning/error/fatal diagnostics. Recorded durations describe qualification work, not runtime performance:

| Configuration | Full build duration | Selected CTest entries | Recorded native case executions |
| --- | ---: | ---: | ---: |
| `opt` | 545.92 s | 34/34 | 2,707/2,707 |
| `dbg` | 881.67 s | 35/35 | 2,707/2,707 |
| `fin` | 533.09 s | 34/34 | 2,712/2,712 |

All 103 CTest executions passed without failures or skips. Debug includes an additional Testbed GPU capture. The selected suites retain existing edge cases and utility integration workflows. The launcher suite separately passed all 40 cases. `build_results.json`, `ctest_results.json`, and `launcher_tests.log` preserve their actual results.

### Runtime and visual checks

The optimized Testbed startup/capture/shutdown check passed with GPU validation. Additional rendered workflows passed their existing assertions and strict runtime log checks:

| Workflow | Observable result | Duration |
| --- | --- | ---: |
| UI raster direct/replay | Four framebuffer pairs are pixel-identical, with maximum channel error 0. | 18.27 s |
| Software CSG analytic shadows | All 26 oracle regions passed. | 4.18 s |
| Testbed gallery navigation | All 13 pages captured through real Next navigation, with exact page markers 2 through 13 and orderly shutdown; the contact sheet was manually reviewed. | 19.67 s |
| TextArea | All 65 native/displayed-state gates passed, retaining exact caret, model, geometry, RGB, and log assertions. | 71.86 s |
| Slider | All 38 native/displayed-state gates passed using the shared GUI-thread capture query. | 34.34 s |

GPU validation was enabled for these rendered workflows. Their normal application logs passed the strict diagnostic checks. Gallery review verifies the recorded pages' visible text, controls, layout, and clipping; this does not claim manual review of every generated capture. `visual_results.json` and the per-workflow reports retain the results.

The first TextArea run passed 62 stages and then failed exact caret equality during fresh focus after resize. Geometry and all 57 RGB markers at that stage passed. Investigation found that the harness posted button release and immediately relocated the physical pointer without observing fixture-thread capture retirement. The evidence is consistent with a later selection update; it does not prove a particular external mouse-message origin. The failed capture, model sequence, and investigation remain preserved in the original TextArea evidence directory and `text_area_focus_investigation.json`.

Four Python files under `tests/` now share the existing GUI-thread capture query, declare the Win32 ABI explicitly, and wait for idle/down/up capture acknowledgment before TextArea pointer relocation. Waits are bounded by two seconds and the overall run deadline. Slider reuses the shared query, and X11 transport remains unchanged. The complete corrected TextArea and affected Slider workflows passed. Exact acceptance assertions and production input behavior were unchanged; static and independent reviews passed. These final changes are Python-only and required no C++ rebuild.

### Source and contract review

Independent combined review checked 802 changed/new C++ sources and headers: UTF-8/no-BOM encoding, CRLF, canonical EOF, required banners, and 128-slash separators passed. No separator-gap deviations were introduced; 16 preexisting deviations across eight files remain unchanged. The retirement scan covered 2,988 first-party non-Markdown files and found no former global-prefix identifiers or old root namespace include paths. Canonical Markdown and wiki references also use the current macro and header names.

The engine namespace header move preserves its contents byte for byte and its canonical two CRLF terminators. The application header is independent of engine includes. The six loader adapter signatures remain unchanged; project policy and lifecycle were reviewed against the original implementation after explicit engine qualification. No compatibility aliases, extra application state, allocations, or runtime dispatch were introduced. Existing shader resource aliases and stable diagnostic markers retain their current contracts. `combined_source_policy_review.json`, `macro_migration_checks.json`, and the independent Testbed review record these checks.

Repository and wiki diff checks passed after final documentation updates. Historical cleanup/optimization audits and their evidence were not rewritten. These checks establish the naming/ownership migration and recorded runtime behavior; they do not establish a CPU, memory, or frame-time gain.

## Configuration and interface follow-up: October 8, 2026

This records the earlier interface removal; the current declaration contract is restored in the next section.

Published source `99845979361d56477e768d4eb4d110fa7533418d` removes the remaining configuration indirection and
project-owned keyword-like annotation. `configuration/CodeGen.cmake` emits `NWB_DEBUG`, `NWB_OPTIMIZE`, and
`NWB_FINAL` directly for `dbg`, `opt`, and `fin`. `global/compile.h` retains the required external compiler configuration
fallbacks and no longer translates intermediate mode names. Twenty interface declarations in sixteen headers use
plain `struct`, matching their former macro expansion. The lowercase annotation and its three Linux-only Wayland
push/undef/pop workarounds were removed, without a new alias. External SDK macros and authored material `interface`
metadata retain their ownership and spelling.

Compiler preprocessing in all three configurations confirms only the intended macro-definition changes. All 135
C++ mode consumers test definition presence. The 126 uses outside `global/compile.h` reach it through unconditional
include paths before their first check; no shared shader/C++ guard or shader consumer changes selection. Plain
`struct` preserves inheritance, access, callback signatures, vtables, and layout. This simplification introduces no
runtime operation, allocation, storage, or dispatch, and claims no measured performance improvement.

This follow-up has separate qualification evidence under `__cmake/verification_macro_followup_20261008/`.
Full `opt`, `dbg`, and `fin` builds passed. Its 103 selected CTest executions passed without failures or skips,
including 2,707 native cases in each of Optimize and Debug and 2,712 in Final; the launcher suite passed 40 cases.
The optimized Testbed validation capture and UI raster direct/replay workflow passed, with all four framebuffer
pairs pixel-identical and maximum channel error zero. These results apply to the recorded Windows ARM64 host.

The normal Final Testbed capture and shutdown passed through CTest. A separate manual Final invocation incorrectly
requested `--gpudbg`, which Final does not support; that failed diagnostic attempt is retained and excluded from
qualification. It is not Final GPU-validation evidence. Manual capture wrappers must use `--no-gpu-validation` with
a `fin` executable; validated runtime checks use `dbg` or `opt`. `qualification_summary.json`, `ctest_results.json`,
`preprocessor_comparison.json`, and `canonical_mode_visibility_review.json` retain the exact results and limits.

## C++ interface spelling restoration: October 8, 2026

Project-owned C++ abstract interfaces again use the lowercase `interface` declaration annotation. `global/compile.h`
owns the portable `interface` to `struct` expansion and defines it only when an external SDK has not already supplied
the spelling. Preserve that SDK definition and isolate collisions with identifiers in external headers at the include
boundary. Ordinary value aggregates and concrete implementations keep their existing `struct` or `class` declarations.

The annotation is the current canonical declaration spelling and an intentional exception to the `NWB_` macro-prefix
rule. Keep it during naming and compatibility cleanup; do not replace it with plain `struct` or introduce a prefixed
declaration wrapper. This changes declaration spelling without adding runtime work or storage. Canonical configuration
macros remain `NWB_DEBUG`, `NWB_OPTIMIZE`, and `NWB_FINAL`; their direct CMake definitions are unchanged.

`.helper/standard.md` and `.helper/note.md` record this restored contract. The earlier removal and its qualification
above remain historical evidence, rather than verification of the restoration.
