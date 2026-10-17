# Custom UI measurement and qualification report

This report preserves substantive measurements and run-specific qualification evidence recorded in `docs/custom_ui_plan.md` before its conversion to a current-design guide. The extracts describe their original source states and hosts; their pass counts, build-mode spellings, revisions, artifact locations, and platform limits are historical evidence, not a claim of fresh execution or current test inventory. Raw output remains in the artifact locations named by each acquisition. Incidental implementation chronology is retained in Git history rather than added here.

Current contracts and operating procedures are in [Custom UI guide](../custom_ui_plan.md). Original source SHA256: `02137d09df9bd85cc1fde5947c44b7ad0f3b9f6a34ca184aee7a36da889e1d23` (UTF-8 text with normalized LF).

## Completion and current-format qualification

Validation before the current-format cleanup: Windows ARM64 / Clang `opt` builds pass for Testbed, the standalone UI executable, Frame, the font builder and affected libraries/tests. All 1,353 toolkit, 201 ECS UI, 103 OS and 17 skin asset tests pass. Font source, atlas payload, bake, quality and font-builder CLI integration suites also pass. Seven native/GPU smokes pass: framebuffer, replacement-skin window/combo/tooltip-and-context-menu/text-area, editor, and direct/command-IR parity. The editor smoke passes 38 displayed/native gates, including alternate clipboard shortcuts, aggregate text/key repeats and double-click selection; its read-only clipboard and final word-selection captures were visually inspected. Genuine Linux x86_64 syntax/type checks pass for 46 translation-unit/configuration combinations across 39 sources, covering Wayland with/without primary selection and X11-only configurations. The final source/header hashes remain stable during that matrix. Changed authored files pass encoding/CRLF and relevant source-format/Python syntax checks; modified C++ files remain below 800 lines. Native Linux execution, live language IME and HDR display qualification remain outstanding.

Validation for the current-format cleanup after merging the concurrent retained-UI and XIM fixes: Windows ARM64 / Clang `opt` builds pass for Testbed and the standalone UI executable. All 1,357 toolkit, 201 ECS UI, 110 OS and 18 skin asset tests pass. Font source, atlas payload, bake, quality and font-builder CLI integration suites pass, as do the skin dependency launcher and pipeline CLI integration. Genuine Linux x86_64 syntax/type checks pass for 45 translation-unit/configuration combinations across 38 sources, including Wayland with/without primary selection and X11-only configurations; all checked source/header hashes remain stable during the run. Six native/GPU smokes pass on the rebuilt merged application: framebuffer, editor, replacement-skin searchable combo/numeric editor/text area, and direct/command-IR parity. The editor passes all 38 native/GPU gates, and its final double-click selection capture was visually inspected. Changed authored files pass encoding/CRLF, source-format and Python syntax checks, and changed C++ files remain below 800 lines.

## October 2026 CPU measurement protocol, results and limitations

Measurements used Windows ARM64 / Clang `opt`, warmed CPU workloads and four alternating baseline/candidate run pairs. Reported times are medians of each run's median, with matching output checksums; paint also compares exact vertex/index/command hashes and shaping compares complete layout hashes outside timed regions. Font validation/serialization uses seven alternating samples of each operation sequence on the real default bundles and compares serialized bytes. These timings describe the named CPU operation or CPU-only frame, not end-to-end FPS, GPU time or total font-bake duration.

| Workload | Before | After | Reduction |
| --- | ---: | ---: | ---: |
| Declare/query/commit 1,024 activation targets | 448.10 us | 118.32 us | 73.6% |
| Declare/query/commit 4,096 activation targets | 8,090.08 us | 517.73 us | 93.6% |
| Declare/claim/commit 4,096 same-kind states | 2,233.95 us | 203.97 us | 90.9% |
| Remove 64 passive labels, retain 128 interactive targets | 101.87 us | 9.54 us | 90.6% |
| Successful lookup in a 1,024-region skin | 161.17 ns | 30.72 ns | 80.9% |
| Paint 512 visible coverage glyphs | 65.25 us | 43.83 us | 32.8% |
| Paint 512 visible baked SDF glyphs | 45.21 us | 36.12 us | 20.1% |
| Multiline baked-font layout | 77.50 us | 69.48 us | 10.3% |
| Default Latin atlas validation and serialization | 52.01 ms | 26.35 ms | 49.3% |
| Default Korean atlas validation and serialization | 421.24 ms | 216.00 ms | 48.7% |

Small-case and memory tradeoffs are explicit. The current 51-region default skin remains approximately unchanged for successful lookup (11.66 ns to 11.82 ns). Always using binary search regressed that case, so the final implementation retains direct scans through 128 regions. State-claim frames at 16/32 entries remain stable or improve; the 33rd claim pays roughly 0.09-0.15 us to promote the prefix. At 64 claims the measured result is approximately flat to 5% faster, at 65 it is 8-10% faster, and the improvement grows with larger panels. Mostly clipped paint and CFF-dominated mixed-font layout are approximately unchanged; sub-2% differences are treated as noise.

The skin index adds four bytes per region (204 bytes for the default skin, 16 KiB at its 4,096-region limit) plus the container object. Context adds a 512-byte inline claim array on this 64-bit build and retains hash capacity after large frames; the 4,096-claim probe uses approximately 107.7 KiB more arena storage than the original vector. Prepared paint adds 48 temporary scratch bytes per conservative glyph candidate beyond the existing index array; a fully visible million-glyph layout can therefore require roughly 48 MiB more transient storage. These caches do not alter GPU textures or the offscreen target format.

Generic CFF outline-bounds caching was rejected: supported static CFF can contain a Type2 `random` operator that mutates FreeType subfont state. Skipping repeated glyph loads could change later output. No font-admission restriction, new parser or compatibility path was added to enable that optimization. Broader renderer changes require a new whole-frame GPU measurement rather than extrapolating these CPU results. Local probes, exact comparison outputs and run logs are retained under `__cmake/ui_optimization/`.

Final qualification: Windows ARM64 / Clang `opt` builds pass for Testbed, the standalone UI, the font builder and affected libraries/tests. All 1,370 toolkit, 201 ECS UI, 25 skin asset, 12 font-atlas asset and six font-bake tests pass, along with the font quality and font-builder CLI integration suites. Eight rebuilt native/GPU smokes pass: framebuffer, editor, replacement-skin searchable combo, tooltip/context-menu, numeric editor, text area and slider, plus direct/command-IR parity. The framebuffer text and final double-click selection captures were visually inspected. Genuine Linux x86_64 syntax/type checking passes for 28 translation-unit/configuration combinations across 21 sources, including Wayland with/without primary selection and X11-only configurations; checked source/header hashes remain unchanged during the run. All 20 changed C++ files pass UTF-8 without BOM, CRLF, banner/EOF and size checks (maximum 663 lines), and `git diff --check` passes. Native Linux linking/compositor execution, live language IME and HDR display qualification remain outside this Windows run.

## Initial paint, clipboard and skin qualification

Validation for this increment on Windows ARM64 / Clang, `opt` configuration:

- The Windows ARM64 configuration and a coordinated build of `nwb_ui_tests`, `nwb_os_tests`, `nwb_assets_ui_skin_tests`, `nwb_asset_builder`, and `testbed` passed.
- All 24 new tests passed: five CPU paint cases, ten clipboard protocol cases, and nine skin cook/load/validation cases. Native clipboard contents are not read or written by these tests.
- The regular Testbed pipeline cooked/gathered 128 assets, including the default skin pair. An isolated pipeline invocation using the default skin directory also completed and produced a runtime `.vol`.
- The existing window-capture smoke launched Testbed successfully and captured a 1280 x 900 scene with its existing ImGui overlay. This validates startup/rendering after OS borrowing integration; it is not evidence of the future custom GPU renderer or IME behavior.
- Modified/new authored sources passed CRLF, banner/separator, exact source EOF, and whitespace checks.

## GPU-layer qualification

Validation for the GPU increment on Windows ARM64 / Clang, `opt` configuration:

- Production libraries, Testbed, the UI-only executable, and the affected unit targets build. The regular pipeline cooks/gathers 132 Testbed assets and the engine-only smoke pipeline cooks/gathers 115 assets after removing `source.png`.
- All 319 GPU task tests and eight related composition/color/timing checks pass. The original five paint, ten clipboard, and nine skin tests also pass after the shared clipboard queue extension.
- `nwb_ui_layer_framebuffer_smoke` captures a completed 960 x 540 acquired backbuffer and passes 16 numeric pixel probes. `nwb_ui_layer_resize_smoke` captures 960 x 540 and 800 x 600 and passes 32 probes. Both request GPU validation and reject warnings, errors, assertions, and abnormal shutdown. Maximum observed channel error is two byte values across these captures.
- A GPU-validation Testbed capture succeeds at 1280 x 900 and after an odd 901 x 607 resize. The custom skin panel and existing scene/ImGui UI are visible, with no rejected log messages.
- The complete ECS graphics suite reports 476 of 477 checks passing. `EcsGraphics.SurfelGbufferNormalsSharePackedDecodeContract` has stale source-text expectations for the existing half-precision normal decoder; the test and its four shader inputs are identical in pulled main `ea95ffb16`, foundation `81481aa88`, and this work. It is left unchanged. There are also 61 pre-existing disabled checks.
- Live captures exercise SDR. HDR composition is covered by tests of the actual shared shader equations; this host has not qualified live HDR presentation.

## Linux OS integration qualification

Validation for the OS increment:

- The final Windows ARM64 / Clang `opt` build of `nwb_os_tests`, Testbed, and the UI-only executable passes. All 21 clipboard cases, five paint cases, nine skin cases, and 319 GPU task cases pass. Both GPU-validation UI pixel smoke tests also pass after the shared Frame/OS changes.
- Actual Linux x86_64 target syntax checks pass for 14 production and five test translation units: 19 with Wayland/primary selection, 19 with Wayland without primary selection, and 16 with X11 only, for 54 successful checks. The disposable harness uses genuine libc/libstdc++/Linux/X11/Wayland headers and generated protocol headers. Saved source/header hashes match the final files; command lines, package/source hashes, and logs are retained under `__artifacts/custom_ui/linux_sysroot/`. This does not qualify Linux ARM64 or linking.
- Linux-only pipe and X11 tests are provided for execution on Linux. This Windows host has no installed WSL, container runtime, or Linux compositor, so live X11/Wayland clipboard behavior and Linux linking remain unqualified.

## Font and shaping qualification

Validation for this increment on Windows ARM64 / Clang, `opt` configuration:

- Production libraries, the font asset tests, UI tests, ECS graphics tests, UI-only executable, and Testbed build. Asset pipelines cook/gather 134 Testbed assets and 117 engine-only smoke assets, including both font defaults and the recooked glyph shader.
- All 26 UI checks pass: the five original paint checks, five glyph snapshot checks, eight layout checks, and eight real-font/shaping/atlas checks. Eight font asset checks and eight related composition/glyph shader checks also pass.
- Both GPU-validation UI smoke tests pass. The acquired-backbuffer capture validates 19 numeric probes at 960 x 540; resize validates 38 probes across 960 x 540 and 800 x 600. Each capture additionally checks Latin/Korean/clipped ink, varied antialiased edges, and absence of ink beyond the text clip. Maximum channel error is two byte values.
- A GPU-validation Testbed capture succeeds at 1280 x 900 and after a 901 x 607 resize, showing the custom labels alongside the existing scene and ImGui overlay. Captures were visually inspected. Live HDR remains unqualified on this host.
- All 22 affected font/text/paint/ECS/GPU production translation units and four new test translation units pass actual Linux x86_64 syntax checks with genuine native headers and the Linux sysroot. This host has no Linux runtime; this is not a Linux link or native-execution claim.
- The known unrelated stale surfel normal source-text expectation recorded above is unchanged; this increment runs the affected ECS composition/glyph checks rather than asserting that the whole ECS suite passes.

## SDF atlas resource sizes, quality gate and qualification

- Engine-default Latin and Korean atlas assets are generated at 32 ppem/spread 8 and selected explicitly by Testbed and the UI-only smoke. They contain 3,748 and 24,964 glyph records respectively, with 5.998046875 MiB and 50.353515625 MiB of compact image bytes. Exact source SFNT and image bytes remain in `.font`, alongside the original license notices.

The qualified policy uses SDF at 0.75..1.5 times the bake ppem in physical pixels, with native coverage outside it. For the default 32-ppem atlases, that is 24..48 physical pixels per em. The current quality test compares Latin A/o/e and Korean Hangul against supersampled native rendering within that interval and requires at least 32 glyph/scale comparisons, maximum edge displacement of one pixel, and maximum mean coverage error of 0.04. Larger candidate magnifications failed the one-pixel edge gate at sharp corners, so this increment does not claim arbitrary zoom quality. The implemented format, utility, and runtime contract are documented in [font_atlas_plan.md](../font_atlas_plan.md).

Validation for this increment on Windows ARM64 / Clang, `opt` configuration:

- Production libraries, utility, asset builder, both sample applications, and affected test targets build. The regular pipeline cooks 136 Testbed assets; the engine-only smoke pipeline cooks 119 assets, including both atlases and the updated shader.
- All 42 UI tests, 12 atlas asset tests, six bake tests, eight source-font asset tests, and 12 affected composition/glyph/color/shader checks pass. Ten real CLI integration cases cover reproducibility, complete font generation, exact positioning bytes, malformed inputs, capacity/overwrite failures, and preservation of previous output. The separate quality test passes 36 comparisons and verifies attachment of both default atlases; all 46 existing repository launcher tests also pass.
- Both GPU-validation UI backbuffer/resize smoke tests pass 75 numeric probes over three captures, plus twelve text checks for ink, clipping, and antialiased edges. Maximum observed channel error is two byte values. A visually inspected Testbed capture passes after resizing to 901 x 607.
- All 55 unique affected production and test translation units pass Linux x86_64 syntax checks using actual Linux headers. Command lines, logs, and final source hashes are retained under `__artifacts/custom_ui/linux_sysroot/`. Native Linux linking, baking equivalence, and GPU execution remain unqualified on this Windows host.

## Interactive foundation qualification

Validation for the interactive increment on Windows ARM64 / Clang, `opt` configuration:

- The UI/input/graphics-presentation/GPU-task/OS CTest suites pass: 94 UI, seven input, 51 presentation, 319 enabled GPU-task, and 21 OS checks (492 enabled checks total; 90 GPU-task checks remain disabled).
- All four GPU-validation smoke tests pass: completed framebuffer, resize, native control interaction, and the scene/Testbed capture. The interaction fixture passes all 14 displayed-state gates and nine exact action-log checks. The 1280 x 900 Testbed capture was visually inspected and shows the interactive custom panel alongside the existing ImGui overlay.
- Linux x86_64 syntax checks pass for 92 distinct production/test translation units using genuine native headers. All 92 per-TU and 200 source/header SHA256 observations remain stable during the run. This host does not qualify Linux linking or native X11/Wayland execution; the two existing Wayland listener-tail initializer warnings remain.
- Authored source checks cover CRLF, UTF-8 without BOM, project banners/separators, exact EOF, definition order, domain separation, and whitespace. Newly authored source files stay below 500 lines.

The Windows input harness uses posted native messages and a real Shift modifier, without physical pointer-grab qualification. Native pointer-leave/capture-loss notifications and exact character/IME event provenance remain follow-up OS/input contracts.

## Window and dependency-retirement qualification

Validation for the M3 increment on Windows ARM64 / Clang, `opt` configuration:

- A fresh CMake build directory with the ImGui directory absent builds the custom toolkit, ECS adapter, Testbed, standalone UI executable, and affected unit targets. Fresh output/cache paths cook and gather 134 Testbed assets and 119 standalone assets (117 engine plus the replacement atlas/texture). No production/build/asset source, compile command, or cooked input manifest retains an ImGui dependency.
- All five core suites pass: 143 UI, 11 input, 51 graphics presentation, 319 enabled GPU task, and 21 OS checks, for 545 enabled checks. The 90 pre-existing GPU task checks remain disabled. Thirteen targeted ECS output-layer, SDR/HDR color, glyph, and timing checks also pass; the unrelated stale surfel-normal source check noted above is outside this filtered run.
- GPU-validation standalone framebuffer, resize, basic interaction, default-window, and replacement-window smoke tests pass. The two window runs each verify 20 displayed stages and six exact action logs; the basic control run verifies 14 stages and nine actions. Default/replacement skin colors and geometry, collapse/body inactivity, minimum resize, drag completion, locking, capture/focus loss, pointer leave, and first interactions after resize are checked against real displayed pixels.
- A scene/Testbed capture with GPU validation passes at a 1280x900 original client and after a 901x607 resize. The original labels/window use the custom font/skin path, with an explicit one-logical-unit separator. Testbed and both skin captures were visually inspected.
- Linux x86_64 syntax validation passes 119 checks over 97 distinct production/test translation units: 97 with primary selection, 12 without it, and 10 X11-only. All 725 source/header hashes per mode remain unchanged, and final current-hash comparison has zero mismatches. Commands and logs remain under `__artifacts/custom_ui/ui_retirement_linux/`. The two existing newer-Wayland listener-tail warning types remain; newly introduced warnings are resolved.
- New/modified authored C++ and asset text pass UTF-8, CRLF, banner/separator gaps, exact source EOF, and whitespace checks. Production window concerns are separate sources of at most 160 lines; the largest newly authored C++ file is the 604-line gesture test source.

M3 removes the ImGui dependency and demonstrates replacement texture/atlas skins. Win32 native message delivery/rendering is qualified here. Linux linking/native X11/Wayland execution, physical pointer grabs, and live HDR remain unqualified on this Windows host. M4/M5 continue the requested edit box, combo box, list, and OS IME work.

## Editing and native-text-service qualification

Validation for this increment on Windows ARM64 / Clang 22.1.4, `dbg` configuration:

- The coordinated build of the production UI/OS/ECS libraries, Testbed, UI smoke executable, and affected unit targets passes. Runtime asset cooking gathers 134 Testbed assets and 119 engine-only smoke assets.
- All 338 unit tests pass: 177 UI, 16 ECS UI, 83 OS, 11 input, and 51 graphics-presentation cases, with no failures or disabled cases in these suites. This includes 112 new tests for editing, text-input services and their ECS adapter; one grapheme case additionally checks all 766 official Unicode 17 conformance rows. The OS suite includes 20 actual hidden-HWND native cases and 21 portable Linux backend-model cases.
- All five GPU-validation UI smoke tests pass: completed-backbuffer painting (25 probes), resize (50 probes across two captures), native interaction (14 observed states), windows (20 observed states), and alternate skin/windows (20 observed states). The GPU-validation Testbed startup/shutdown capture also passes and is visually inspected at 1280 x 900.
- Linux x86_64 target syntax qualification passes 201 C++ checks: 124 with Wayland/text-input-v3/primary selection, 27 without primary selection, 27 without text-input-v3, and 23 with X11 only. The protocol header/private C source are generated by the actual upstream Wayland scanner; the generated C also passes a target syntax check. Ten affected C++ checks are repeated after final formatting changes. Genuine Linux libc/libstdc++/X11/Wayland headers, commands, source hashes, generation provenance and logs are retained under `__artifacts/custom_ui/`.
- Modified/new authored C++ sources pass UTF-8, CRLF, exact separator/EOF and whitespace checks. New production translation units are split by domain and remain below 650 lines; the existing Frame Wayland file receives only native forwarding/lifetime hooks.

## Superseded font authoring transport evidence

This earlier increment published one authoring schema 2 `.nwb` containing glyph metadata, all RGBA groups, and the original OpenType positioning tables. Each decoded payload was compressed into a deterministic Zstd frame and embedded as canonical base64 chunks. It removed hash-named payload sidecars. The explicit binary font/atlas contract below supersedes this authoring transport; this section retains the historical validation record.

- `global/base64.h` owns bounded canonical encoding/decoding, including failure preservation and aliased input/output.
- At that increment, `impl/assets_font_atlas/source_payload.*` owned the shared source compression contract, and `cook_metadata_payload.cpp` validated chunks and frame limits before publishing decoded data. The cooker then retained legacy authoring schema 1 imports, and the runtime `FTA1` format was version 1.
- The former text generator built a complete document separately from exclusive temporary-file staging and final publication. The intermediate three-file builder later staged and verified its binary font, atlas and declaration; the current two-file publication contract below supersedes that stage as well.
- The default Latin and Korean atlases are regenerated into one `.nwb` each, and their ten old sidecars are removed. Independent decoding verifies unchanged source-font hashes, 3,748 / 24,964 glyph records, 2 / 4 RGBA groups, and exact original `GPOS`/`GDEF` bytes. The new files contain 5,176,115 / 51,901,890 bytes, compared with 9,094,378 / 71,528,974 bytes for their previous metadata-plus-sidecar packages.

Validation on Windows ARM64 / Clang 22.1.4, `dbg` configuration:

- Seven base64, eighteen font-atlas asset, and six bake tests pass. Asset coverage exercises deterministic compression, corrupt or unsupported frames, malformed chunk layout, bounded admission, isolated-file cooking, and preservation on failure.
- The eleven-case CLI integration suite passes with the real utility and importer. It verifies single-file publication and relocation into an otherwise empty directory, exact decoded texture/table hashes, all-glyph coverage, channel packing, repeated byte-identical output, overwrite behavior, failed bakes, and occupied temporary-file preservation.
- The production cook/gather passes for 134 Testbed assets and 119 engine-only smoke assets after sidecar removal. All 177 UI tests and the default-atlas SDF quality case pass, including its 36 Latin/Korean comparisons against supersampled native outlines across the qualified zoom/DPI range.
- GPU-validation framebuffer, resize and Testbed capture tests pass. Standalone captures pass 25 / 50 numeric probes; current standalone and 1280 x 900 Testbed text captures are visually inspected.
- Sixteen affected C++ translation units pass Linux x86_64 syntax checks using genuine Linux libc/libstdc++ headers and the existing vendored dependency headers, with no final diagnostics. Native Linux linking, execution and cross-platform bake byte equivalence remain unqualified on this Windows host.
- All fourteen changed/new authored C++ files pass UTF-8, CRLF, exact separator/EOF and whitespace checks. The largest new translation unit is the 357-line source-import test; production sources are split by compression, import, metadata and publication domains.

## Compact source-pair storage comparison

Current default identities are `engine/ui/fonts/default/latin/face` and `/latin/atlas`, plus their `/korean/face` and `/korean/atlas` equivalents. Both faces bake at 32 ppem with spread 8. Latin's 1024 × 1024 RGBA8 and 1024 × 1023 RG8 groups use 5.998046875 MiB; Korean's two 2048 × 2048 RGBA8 groups, one 2048 × 2047 RGBA8 group and one 2048 × 1209 R8 group use 50.353515625 MiB. Combined image bytes fall from 72 to 56.3515625 MiB, saving 15.6484375 MiB (21.733941%). Regeneration preserved every glyph's SDF and exterior-guard bytes, source hash, metrics and positioning tables. GPU allocations can include driver alignment and tiling overhead.

## Single-line editor qualification

Validation for this increment on Windows ARM64 / Clang 22.1.4, `dbg` configuration:

- The coordinated native build of the production libraries, Testbed and standalone UI smoke executable passes.
- All 402 unit tests pass: 198 UI, 59 ECS UI, 83 OS, 11 input and 51 graphics-presentation cases. Coverage includes visual LTR/grapheme geometry, edit command policy, normalized paste, immutable Copy lifetime, fenced asynchronous cut/paste, ordered native/keyboard input, and external replacement.
- `nwb_ui_layer_edit_smoke` passes all 26 native/GPU displayed-state gates with GPU validation. Matching numeric model/sequence pixels, skin, selection, caret and clip probes verify Unicode editing, clipboard/history, read-only/focus/hidden/replacement boundaries, interleaved commit/arrow/commit, resize and horizontal scroll. The Win32 path uses synthetic character messages and actual Control/Shift modifiers; it does not qualify live IME composition or physical pointer grabs.
- All seven GPU-validation smoke tests pass: the new editor sequence, existing framebuffer, resize, basic interaction, default-window, alternate-skin/window and scene/Testbed captures. Visual inspection confirms the selected Korean grapheme and the new editable/read-only Testbed gallery.
- Linux x86_64 target syntax checks pass for 39 affected production/test translation units using genuine Linux headers. Commands, source hashes and logs remain under `__artifacts/custom_ui/edit_box_linux/`. Native Linux linking, compositor execution and live XIM/text-input-v3 behavior remain unqualified on this Windows host.

## Popup and modal qualification

Validation for this increment on Windows ARM64 / Clang 22.1.4, `dbg` configuration:

- The native build of the production libraries, Testbed and standalone UI smoke executable passes.
- All 489 unit tests pass: 278 UI, 66 ECS UI, 83 OS, 11 input and 51 graphics-presentation cases. New coverage includes placement, layer/clip ordering, atomic scope publication, focus return chains, held-sequence and old-epoch fencing, body close/reopen, pending native-focus loss, borrowed session handoff, transient preedit Escape and stale paste/native/key intentions.
- All nine GPU-validation smoke tests pass: default/alternate popup sequences plus editor, framebuffer, resize, basic interaction, default/alternate windows and scene/Testbed capture. Each popup sequence passes 31 displayed-state gates using matching numeric model/sequence pixels, popup skin, late-root overlap and modal-dimming probes. Visual inspection confirms both skin families and modal composition.
- Genuine Linux x86_64 syntax/type checks pass for 90 distinct production/test translation units, including X11 and Wayland clipboard/text-input backends. The report records per-TU commands/hashes and compiler dependency files. All 9,815 source/header/include inputs and 1,038 actual dependencies remained unchanged, with no missing dependency hashes or compiler failures. Results are under `__artifacts/custom_ui/popups_linux/`.
- All 45 changed C++ source/header files pass UTF-8 without BOM, CRLF, banner/separator and exact EOF checks; the largest is 636 lines. `git diff --check` and changed smoke Python syntax checks pass.

The native Windows harness uses posted Win32 messages and native modifier state; synthetic text bypasses live IME composition. The X11 harness is provided separately. Linux syntax/type checks do not qualify native linking, X11/Wayland compositor execution or live XIM/text-input-v3 behavior. Physical pointer grabs, live HDR, nested popup layouts, tooltips/context menus, scrolling lists and combo boxes retain separate scope.

## Combo qualification

Qualification on Windows ARM64 / Clang 22.1.4, `dbg`: all 674 native unit tests pass (UI 463, ECS UI 66, OS 83, input 11, graphics presentation 51), including 67 new combo tests. All 13 GPU smoke tests pass with graphics validation, including 64 matching displayed/native/pixel gates for each combo skin. The 100,000-row combo fixture reads at most 10 labels per frame. Genuine Linux x86_64 syntax/type checks pass for 127 translation units covering shared UI plus X11/Wayland input, text-input-v3 and primary selection; 9,857 source/include inputs and all 1,120 actual compiler dependencies remain unchanged through the run. These checks do not establish Linux native linking/compositor execution, physical pointer grabs or live IME. All 28 changed C++ files pass UTF-8, CRLF, banner/separator, exact EOF and source-size checks; the largest is 662 lines. The default generator and checked-in atlas agree by semantic region, and both skins resolve all five combo states using existing texture tiles.

## Search-combo qualification

Qualification on Windows ARM64 / Clang 22.1.4, `dbg`: all 745 native UI/ECS-UI/OS/input/presentation tests pass, including 71 new query, keyboard-owner, Builder and queued OS-service regressions. All 483 executed ECS graphics tests also pass after the main merge; 61 existing tests in that suite remain disabled. All 15 UI/Testbed GPU smoke tests pass with graphics validation, including 43 displayed/native/pixel gates per searchable-combo skin. Both search fixtures read at most 10 row/field labels per frame over 100,000 rows. Genuine Linux x86_64 parsing/type checks pass for 147 translation units with X11/Wayland text-input-v3 and primary selection enabled; 9,873 hashed source/include inputs and all 1,148 actual dependencies remain unchanged. All 41 changed C++ files pass source-format and size checks; the largest is 615 lines and the largest production file is 399 lines.

## Tooltip and context-menu qualification

Qualification on Windows ARM64 / Clang 22.1.4, `dbg`: all 835 relevant native UI/ECS-UI/OS/input/presentation tests pass (619/71/83/11/51), including 90 new tooltip, trigger, Builder, deferred-loan and focus-restoration regressions. All 15 existing UI/Testbed GPU smokes and both new popup-tools smokes pass with graphics validation. Each new skin fixture completes 45 displayed/native/pixel gates; menu painting reads at most five command labels per frame. Win32 fixtures position/restore the physical cursor, prepare the raw client once, and use posted buttons/keys plus native modifiers. That evidence does not qualify physical pointer grabs or live IME. Genuine Linux x86_64 parsing/type checks pass for 164 translation units with X11/Wayland text-input-v3 and primary selection enabled; all 9,898 hashed inputs and 1,173 actual dependencies remain unchanged. All 48 changed C++ files pass source-format/size checks (largest test 640 lines; largest production source 423 lines).

## Scope-storage qualification

Validation on Windows ARM64 / Clang, `dbg`: all 619 UI and 71 ECS-UI tests pass. The window, edit, popup, combo, searchable-combo and popup-tools GPU smoke tests all pass. Genuine Linux syntax/type checks pass for 165 translation units with unchanged input and dependency hashes; native Linux linking and compositor behavior remain unqualified. All 22 changed C++ sources pass the source-format and size checks, and `git diff --check` passes.

## Nested popup qualification

Nested qualification on Windows ARM64 / Clang, `dbg`: all 688 toolkit and 75 ECS UI tests pass, including three-level automatic popup triggers, four source-callback Builder reentry attacks, queued native/clipboard ownership and different-anchor secondary-menu focus restoration. Both nested skins pass 48 native/displayed/pixel gates. Fifteen unaffected existing UI/Testbed GPU fixtures pass; the two popup-tools regressions are separately qualified after isolating held-key checks from anchor hover and maintaining the requested Win32 cursor during captures. Frozen nested captures were visually inspected for parent restoration, escaped child clips and alternate query appearance.

Genuine Linux x86_64 syntax/type checking passes for 178 distinct translation units with X11 and Wayland text-input-v3/primary selection definitions. All 9,918 hashed inputs and 1,193 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking, compositor execution, physical pointer grabs and live IME remain unqualified on this Windows host. All 61 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and 800-line size checks; the largest is 698 lines. Changed Python drivers pass AST/encoding/line-ending checks, and `git diff --check` passes.

## Ordered edit-action qualification

Qualification on Windows ARM64 / Clang, `dbg`: all 688 toolkit and 98 ECS UI tests pass, including 23 ordered-action traces. The ordinary edit, searchable-combo and nested-popup GPU regressions pass. Genuine Linux x86_64 syntax/type checking passes for 181 translation units; all 9924 hashed inputs and 1,199 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking, compositor execution and live IME remain unqualified. All 12 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 384 lines); authored text and `git diff --check` also pass.

## Numeric-editor qualification

Qualification on Windows ARM64 / Clang, `dbg`: all 772 toolkit and 98 ECS UI tests pass, including 49 numeric model/parser/formatter cases and 35 Builder action/lifetime cases. The ordinary edit, searchable-combo and nested-popup GPU regressions pass. Both numeric GPU fixtures pass all 77 native/displayed/pixel gates with default and replacement skins. Frozen captures were visually inspected. The fixtures exercise exact integers above 2^53, malformed/incomplete drafts, real OS clipboard copy/cut/paste, undo/redo, bounds, policy/focus changes, external replacement, ordered Submit input, resize and long-draft scrolling with default and replacement skins.

Genuine Linux x86_64 syntax/type checking passes for 196 translation units; all 9949 hashed inputs and 1224 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking, compositor execution and live IME remain unqualified on this Windows host. All 35 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 364 lines); authored Python passes AST/encoding/line-ending checks and `git diff --check` passes.

## Multiline model qualification

Qualification on Windows ARM64 / Clang, `dbg`: all 826 toolkit and 120 ECS UI tests pass. The new cases exercise multiline admission/normalization, hard-line commands, preedit byte offsets, one-operation clipboard/native history, and identical or away-and-back selection/composition lifetimes through queued services and deferred numeric/search callbacks. The ordinary edit, searchable-combo and numeric GPU regressions pass.

Genuine Linux x86_64 syntax/type checking passes for 206 translation units; all 9961 hashed inputs and 1236 actual dependencies remain unchanged during qualification, with no missing dependency hashes. The single corrected popup-focus assertion source was rechecked against the frozen inputs after the full batch. Native Linux linking, compositor execution and live IME remain unqualified on this Windows host. All 37 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 310 lines); authored text and `git diff --check` also pass.

## Multiline caret geometry qualification

Qualification on Windows ARM64 / Clang, `dbg`: all 877 toolkit and 120 ECS UI tests pass, including 36 direct geometry and 15 multiline View cases. The ordinary edit, searchable-combo and numeric GPU regressions pass.

Genuine Linux x86_64 syntax/type checking passes for 211 translation units; all 9969 hashed inputs and 1244 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking, compositor execution and live IME remain unqualified on this Windows host. All 12 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 435 lines); authored text and `git diff --check` also pass.

## Ordered vertical-navigation qualification

Qualification on Windows ARM64 / Clang, `dbg`: all 887 toolkit and 160 ECS UI tests pass, including ten navigation-state/translation and forty ordered host/lifetime/hit cases. The ordinary edit, searchable-combo and numeric GPU regressions pass.

Genuine Linux x86_64 syntax/type checking passes for 220 translation units; all 9980 hashed inputs and 1255 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking, compositor execution and live IME remain unqualified on this Windows host. All 20 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 390 lines); authored text and `git diff --check` also pass.

## Text-area qualification

Qualification on Windows ARM64 / Clang, `dbg`: all 959 toolkit and 160 ECS UI tests pass. The ordinary edit, searchable-combo and numeric GPU regressions pass, and both text-area skins pass all 42 displayed/native/pixel gates, including completed cross-line Cut and one Undo, canonical clipboard history, preferred-column navigation, resize and focus retirement.

Genuine Linux x86_64 syntax/type checking passes for 235 translation units; all 10,001 hashed inputs and 1,276 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking, compositor execution and live IME remain unqualified on this Windows host. All 33 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 521 lines); authored text and `git diff --check` also pass.

## Two-axis scrolling qualification

Qualification on Windows ARM64 / Clang, `dbg`: all 1,028 toolkit and 164 ECS UI tests pass. Both text-area skins pass all 65 displayed/native/pixel gates, with 57 markers per gate. The ordinary edit, default/replacement list, searchable-combo and numeric GPU regressions pass with graphics validation. The two corrected unit fixtures retain their original behavior checks while accounting for reserved scrollbar height and fresh-parent popup retirement. Native focus clicks stay inside the text viewport and park away from bars for strict normal-atlas keyboard probes.

Genuine Linux x86_64 syntax/type checking passes for 245 translation units; all 10,014 hashed inputs and 1,289 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking/compositor execution and live IME remain unqualified on this Windows host. All 29 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 559 lines); authored text and `git diff --check` also pass.

## Visible-glyph qualification

Qualification on Windows ARM64 / Clang, `dbg`: all 1,077 toolkit and 164 ECS UI tests pass. The font quality test passes all 36 supersampled comparisons. All 22 native UI GPU fixtures pass with graphics validation, including both text-area skins and their 65 displayed/native/pixel gates. The revised caret fixture checks both early layout rejection and defensive rejection of malformed adopted geometry while preserving the prior ready geometry.

Genuine Linux x86_64 syntax/type checking passes for 267 translation units; all 10,022 hashed inputs and 1,370 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking/compositor execution and live IME remain unqualified on this Windows host. All 20 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 486 lines); authored text and `git diff --check` also pass.

## Radio-group qualification

Qualification on Windows ARM64 / Clang, `dbg`: all 1,171 toolkit and 164 ECS UI tests pass, including 94 new radio and horizontal-navigation cases. Both radio skins pass all 33 displayed/native/pixel gates with graphics validation. The ordinary edit, list, searchable-combo and nested-popup GPU regressions pass. Frozen captures were visually inspected. The native fixtures exercise disabled checked choices, keyed reorder/removal/source replacement, one-host Tab navigation, held-key retirement, pointer capture, resize, popup ancestry and focus loss. Their pixel oracle reports fully popup-occluded background samples explicitly while retaining strict visible atlas and model-marker checks.

Genuine Linux x86_64 syntax/type checking passes for 288 translation units; all 10,053 hashed inputs and 1,401 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking/compositor execution and live IME remain unqualified on this Windows host. All 46 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 699 lines); all nine authored text files pass encoding/line-ending checks, Python AST checks pass, and `git diff --check` passes.

## Slider qualification

Qualification on Windows ARM64 / Clang, `dbg`: all 1,269 toolkit and 164 ECS UI tests pass, including 98 new slider, Builder, lifetime, popup and copied-pointer-value cases. Both slider skins pass all 38 displayed/native/pixel gates with graphics validation, and frozen captures were visually inspected. The ordinary edit, searchable-combo, nested-popup, text-area and radio-group GPU regressions pass. The native fixture isolates each requested Win32 cursor point, releases that constraint for focus-loss checks and restores the previous cursor bounds and position during cleanup; its 128-entry action trace distinguishes requested input from observed native movement. Posted-message qualification does not establish physical pointer-grab behavior.

Genuine Linux x86_64 syntax/type checking passes for 308 translation units; all 10,081 hashed inputs and 1,429 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking, compositor execution and live IME remain unqualified on this Windows host. All 43 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 557 lines); all nine authored text files pass encoding/line-ending checks, Python AST checks pass, and `git diff --check` passes.

## Progress qualification

Qualification on Windows ARM64 / Clang, `dbg`: all 1,302 toolkit and 164 ECS UI tests pass, including 23 new layout and 10 new Builder progress cases. Both progress skins pass all 12 displayed/native/pixel gates with graphics validation; both slider skins retain their 38 passing gates after extracting the shared cursor guard. Frozen default and replacement-skin captures were visually inspected, including nested popup draw order, tiny fills and external clipping.

Genuine Linux x86_64 syntax/type checking passes for 316 translation units; all 10,096 hashed inputs and 1,444 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking, compositor execution and live IME remain unqualified on this Windows host. All 24 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 479 lines); all ten authored text files pass encoding/line-ending checks, Python AST checks pass, and `git diff --check` passes.

## Atlas-image qualification

Qualification on Windows ARM64 / Clang, `dbg`: all 1,323 toolkit and 164 ECS UI tests pass, including 10 new layout and 11 new Builder image cases. Both image skins pass all 10 displayed/native/pixel gates with graphics validation; both progress skins retain their 12 passing gates. Default and replacement-skin captures were visually inspected, including copied declaration values, transparency, external clipping and nested popup order. The rejection fixtures establish independent accepted frames because an explicit input abandonment intentionally clears targets.

Genuine Linux x86_64 syntax/type checking passes for 324 translation units. The final affected rejection-fixture source is rechecked after the full batch; all 10,110 final hashed inputs and 1,458 actual dependencies are verified, with no unrelated changes or changes during either qualification stage. Native Linux linking, compositor execution and live IME remain unqualified on this Windows host. All 23 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 334 lines); all eight authored text files pass encoding/line-ending checks, Python AST checks pass, and `git diff --check` passes.

## Owned-texture-source qualification

Qualification on Windows ARM64 / Clang, `dbg`: the separate `nwb_ui_images` library builds, and all 1,344 toolkit tests pass. The 21 new cases exercise exact copies, caller asset/arena destruction, same-path replacements, supported color/alpha transports, shape/payload rejection and real typed codec/manager loading. Independent review closed move-based payload removal and construction through the public reference-count wrapper; three genuine Linux compiler probes reject source moves, direct wrapper construction and generic conversion-token forgery.

Genuine Linux x86_64 syntax/type checks pass for all four new production and unit-fixture translation units. All 10,116 hashed inputs and 730 actual dependencies remain unchanged, with no missing hashes. This standalone CPU increment changes no GPU or OS execution path. Native Linux linking/compositor execution and live IME remain unqualified. All six new C++ sources pass encoding, CRLF, banner/separator, exact EOF and size checks (maximum 439 lines); all four authored documentation/build files pass UTF-8 without BOM and CRLF checks, and `git diff --check` passes.

## Texture-descriptor admission qualification

The Windows ARM64 / Clang `dbg` build of Testbed and the standalone UI executable passes. Both default and replacement-skin image fixtures pass their ten displayed/native/pixel gates with graphics validation, exercising real successful texture loading, decoding, upload and sampling. Genuine Linux x86_64 syntax/type checking passes for the changed loader translation unit; all 10,116 hashed inputs and 783 actual dependencies remain unchanged. The 481-line source passes the format/size audit. Failure cleanup and operation order were reviewed directly; descriptor-write and upload-rejection fault injection are not established by these successful native fixtures. No new broad product test hooks were introduced.

## Immutable-image binding qualification

Qualification on Windows ARM64 / Clang, `dbg`: all 1,365 toolkit tests and 164 ECS UI tests pass. The 21 new cases cover copied and frozen source ownership, UV clipping, premultiplied HDR tint, no-op admission, cross-kind capacity, generation-aware batching, and concrete GPU snapshot admission. Testbed and the standalone UI executable build. Both concrete-image skins pass all 14 displayed/native/pixel gates, including same-path payload replacement, 72 callback-created and displayed versions through cache eviction, mixed font/skin/solid/image rendering, resized clipping and manual primitive overlay order. Both existing atlas-image skins retain their ten passing gates. Default and alternate captures were visually inspected.

Genuine Linux x86_64 syntax/type checks pass for 343 translation units; all 10,128 hashed inputs and 1,540 actual dependencies remain unchanged, with no missing dependency hashes. This is compilation evidence, without native Linux linking/compositor or live IME execution. All 25 changed C++ sources pass encoding, CRLF, banner/separator, exact EOF and size checks (maximum 363 lines); all eight authored build/documentation/shader/driver text files pass UTF-8 without BOM and CRLF checks, both Python drivers parse, and `git diff --check` passes.

## Builder engine-image qualification

Qualification on Windows ARM64 / Clang, `dbg`: the toolkit, ECS UI, standalone UI and Testbed targets build. All 1,387 toolkit tests and 164 ECS UI tests pass, including 16 Builder and six layout cases for source/options retention, same-path versions, natural size independent of skin density/DPI, passive annotations, nested popup ownership/closure, atomic invalid admission and visible versus clipped capacity. Both engine-image skins pass all 15 displayed/native/pixel gates. Their parent and child images use actual Builder popup frames; no manual source overlay or chrome is used. Both atlas-image regressions and the Testbed GPU-validation capture also pass. Source replacement, tint and actual nested popup captures were visually inspected.

Genuine Linux x86_64 syntax/type checks pass for 347 translation units, including the texture paint primitive and both new Builder/layout tests. All 10,131 hashed inputs and 1,544 actual dependencies remain unchanged, with no missing hashes. Native Linux linking/compositor execution and live IME remain unqualified on this Windows host. All 20 changed C++ sources pass encoding, CRLF, banner/separator, exact EOF and size checks (maximum 571 lines); all six authored build/documentation/driver files pass encoding/line endings, both Python files parse, and `git diff --check` passes.

## Wayland selection-excluded deletion qualification

Qualification on Windows ARM64 / Clang, `dbg`: the toolkit, ECS UI, OS, standalone UI and Testbed targets build. All 1,399 toolkit tests, 179 ECS UI tests and 97 OS tests pass, including 41 new selection-deletion regressions and the existing Win32 hidden-window/message tests. The editor, default text-area and replacement-skin text-area displayed/native regressions pass with graphics validation. These fixtures preserve legacy Caret behavior; they do not establish live Win32 IME or native Wayland compositor behavior.

Genuine Linux x86_64 syntax/type checks pass for 352 translation units. All 10,135 hashed inputs and 1,550 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking/compositor execution and live IME remain unqualified on this Windows host. All 15 changed C++ sources pass encoding, CRLF, banner/separator, exact EOF and size checks (maximum 461 lines); all seven authored build/documentation files pass encoding and line-ending checks, and `git diff --check` passes.

## Targeted skin dependency qualification

Qualification on Windows ARM64 / Clang, `dbg`: the pipeline, graphics asset tests, skin asset tests, and terminal utilities build. The existing launcher suite (46 cases), three focused opt-in launcher tests, terminal CLI integration and all nine skin asset tests pass. The pipeline CLI integration passes its 16 direct manifest cases, exact two-asset cook/gather volume, complete-block UASTC sidecar recook and repack, successful opt-in launcher, failed-publication preservation and malformed unrelated bind check. Three actual shader/material cook regressions pass, confirming selected graphics preparation still emits and loads its shader archive. The default authored atlas now has 51 regions; its round-trip test compares loaded regions against the cooked source and continues to verify concrete region geometry and texture identity.

Genuine Linux x86_64 syntax/type checks pass for seven changed translation units. All 8,420 hashed C++/Slang/header inputs and 831 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking/execution was not run. All 11 authored C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 528 lines); all nine authored build/documentation/Python files pass UTF-8 without BOM and CRLF checks, all four Python sources parse, and `git diff --check` passes.

## Font image transport quality decision

The original overview records that the UASTC quality gate failed and qualified lossless font-image transport was retained. It provides no additional numeric UASTC acquisition results; the current contract remains lossless transport.
