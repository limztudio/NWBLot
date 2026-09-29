# Custom UI and offscreen composition plan

Status: implementation is on `custom_ui`, with `main` merged through `e2402c958` in `acea8bca5`. M3 window/separator parity and complete ImGui retirement are pushed in `87f2879a8`. The M4 foundation includes an owned Unicode edit model, OS text-input/IME and clipboard services for Win32 and Linux, a borrowed ECS edit-session adapter, and single-file SDF font atlas packaging. M5 now includes skinned single-line edit boxes, popup/modal scopes, keyed virtualized lists, ordinary/searchable combo boxes, delayed tooltips, stable-key context menus and nested user-popup composition. Independent UI GPU preparation and final framebuffer composition remain shared by these controls. Numeric editors now preserve exact i64/finite f64 values independently of editable drafts; multiline content and caret geometry precede the dedicated text-area Builder/host increment. Native Linux compositor/live IME qualification remains outstanding. Remaining work proceeds as separate committed and pushed increments, stopping before M6 measurement and tuning.

Implemented in the first increment:

- `impl/assets_ui_skin/`: typed skin/texture identities, named sprite and nine-slice regions, logical metrics, versioned binary codec, atomic loading, MetaScript cook entry, and runtime/cook registration. Concrete schema version 1 is documented in `impl/assets_ui_skin/README.md`.
- `impl/assets/ui/skins/default/`: a generated 256 x 256 atlas with 41 named regions, atlas `.nwb`, and the normal converted texture `.nwb`/`.tex` pair. `utilities/ui_skin/generate_default.py` reproduces the artwork and atlas metadata. The intermediate `source.png` is omitted from the repository and runtime assets.
- `impl/ui/`: arena-owned CPU paint builder, nested rectangular clipping with UV trimming, sprite/nine-slice geometry, adjacent batching in painter order, and move-only immutable snapshots with frame/display/skin generation metadata.
- `core/os/`: queued clipboard requests/completions with copied UTF-8 text, capability reporting, cancellation, and service/request generation checks. Windows has a native `CF_UNICODETEXT` backend; other platforms and primary selection explicitly report unsupported capabilities.
- `core/frame/`, `loader/`, and `impl/ecs_ui/`: Frame owns/pumps the clipboard service on the event thread and passes a required borrowed reference through the project context to ECS UI callbacks. Native window lifetime encloses service lifetime; project borrowers are destroyed first.
- Deterministic CPU, clipboard protocol, and skin cook/load tests, including the real engine-default atlas and texture. Schema version 1 limits skins to 4096 regions and rejects larger counts before allocation or copying.

This foundation increment is part of M1. GPU composition, font/text, and initial widget identity/layout/input are implemented in the following increments; IME and the requested edit box/combo/list remain subsequent work. Existing ImGui rendering remains active. `AssetRef<T>` identifies an asset; it does not retain a loaded version or GPU resource. CPU snapshots copy the needed atlas metadata, and the GPU adapter retains concrete resolved resource versions through completion.

Validation for this increment on Windows ARM64 / Clang, `opt` configuration:

- `cmake --preset windows-clang-arm64` and a coordinated build of `nwb_ui_tests`, `nwb_os_tests`, `nwb_assets_ui_skin_tests`, `nwb_asset_builder`, and `testbed` passed.
- All 24 new tests passed: five CPU paint cases, ten clipboard protocol cases, and nine skin cook/load/validation cases. Native clipboard contents are not read or written by these tests.
- The regular Testbed pipeline cooked/gathered 128 assets, including the default skin pair. An isolated pipeline invocation using the default skin directory also completed and produced a runtime `.vol`.
- The existing window-capture smoke launched Testbed successfully and captured a 1280 x 900 scene with its existing ImGui overlay. This validates startup/rendering after OS borrowing integration; it is not evidence of the future custom GPU renderer or IME behavior.
- Modified/new authored sources passed CRLF, banner/separator, exact source EOF, and whitespace checks.

Implemented in the GPU increment:

- `impl/ui/gpu/`: separate resource, graph declaration, raster, and standalone presentation sources. The renderer pins concrete skin textures, geometry, render targets, samplers, pipelines, and descriptor allocations through accepted GPU completion. Three target/buffer slots bound admission; rejected submissions retain immutable CPU data and every accepted prefix token.
- `core/task/gpu/output_layer_contributor.h`: a generic `interface` contract for independent layer production and final-consumer acceptance. UI uploads, transparent clear, and raster produce a full-output-size `RGBA16_FLOAT` premultiplied linear layer without a scene dependency. The final scene compositor consumes its explicit resource version.
- `impl/assets/graphics/ui/` and the existing final compositor: skin raster shaders, standalone output, SDR composition after scene display mapping, and HDR composition in linear nits before one PQ encoding. UI paper white is 203 nits and remains independent of scene exposure.
- `impl/ecs_ui/layer_system*`: a main-thread ECS adapter with an explicit scene/standalone presentation choice and borrowed OS service reference. The renderer receives frozen paint data instead of live ECS callbacks.
- Testbed shows a separate default-skin preview. Existing ImGui labels/window are in `ui_controls.cpp`; skin preview painting is in `ui_skin_preview.cpp`. The preview exercises appearances and clipping; interactive controls and fonts are still toolkit work.
- `tests/smoke/ui_layer/`: a UI-only executable, engine-only asset cook, completed-backbuffer readback, resize capture, and numeric SDR pixel acceptance. It exercises two empty startup snapshots, solid and skin painting, transparency, nested clips, and cleared margins without a scene renderer or ImGui system.

This implements the geometry and composition path in M2. Text/font output and the full milestone exit gates remain open. `ui.raster` and `ui.output` have task timing; existing `render.frame` measures scene begin through final presentation and excludes independent UI work before scene begin. Native UI draw recording does not yet have a complete command-IR replay adapter. The current void render-pass API cannot abort an acquired standalone frame after rejection; the ECS adapter requests device recreation on that failure.

Validation for the GPU increment on Windows ARM64 / Clang, `opt` configuration:

- Production libraries, Testbed, the UI-only executable, and the affected unit targets build. The regular pipeline cooks/gathers 132 Testbed assets and the engine-only smoke pipeline cooks/gathers 115 assets after removing `source.png`.
- All 319 GPU task tests and eight related composition/color/timing checks pass. The original five paint, ten clipboard, and nine skin tests also pass after the shared clipboard queue extension.
- `nwb_ui_layer_framebuffer_smoke` captures a completed 960 x 540 acquired backbuffer and passes 16 numeric pixel probes. `nwb_ui_layer_resize_smoke` captures 960 x 540 and 800 x 600 and passes 32 probes. Both request GPU validation and reject warnings, errors, assertions, and abnormal shutdown. Maximum observed channel error is two byte values across these captures.
- A GPU-validation Testbed capture succeeds at 1280 x 900 and after an odd 901 x 607 resize. The custom skin panel and existing scene/ImGui UI are visible, with no rejected log messages.
- The complete ECS graphics suite reports 476 of 477 checks passing. `EcsGraphics.SurfelGbufferNormalsSharePackedDecodeContract` has stale source-text expectations for the existing half-precision normal decoder; the test and its four shader inputs are identical in pulled main `ea95ffb16`, foundation `81481aa88`, and this work. It is left unchanged. There are also 61 pre-existing disabled checks.
- Live captures exercise SDR. HDR composition is covered by tests of the actual shared shader equations; this host has not qualified live HDR presentation.

Implemented in the Linux OS increment:

- `core/os/clipboard_service*` and `clipboard_text*`: the shared queue now supports delayed native completion while preserving copied input, FIFO admission, bounded requests, cancellation, and stale-token rejection. UTF-8 validation, bounded chunk accumulation, and Latin-1 conversion belong to the OS domain. The existing Win32 backend keeps the same public interface and native `CF_UNICODETEXT` path.
- `core/os/linux/x11/`: a native clipboard/primary-selection service borrowing Frame's display, with separate request windows, server timestamps, target negotiation, `MULTIPLE`, and bounded `INCR` transfers. Retained outgoing bytes survive selection loss; stale notifications cannot complete replacement requests or erase reacquired ownership. Foreign-window errors and event-mask restoration are handled in their own helper.
- `core/os/linux/wayland/`: core data-device clipboard and optional primary-selection protocol support, separate offers/sources, bounded native pipe progress, and focus/serial eligibility. Same-seat manager changes preserve focus and require a fresh input serial; seat loss detaches the service before native seat destruction. Missing optional protocol support is reported through capabilities.
- `core/os/linux/pipe_io*`: descriptor ownership, nonblocking local progress, copied outgoing text, UTF-8 validation at EOF, transfer budgets/deadlines, and thread-local handling of broken-pipe signals. The incoming pipe leaves the foreign source's write endpoint blocking.
- `core/frame/`: platform factories use the existing Win32 window or active Linux display/seat. X11 clipboard events are dispatched before ordinary window input; helper-window destruction cannot close the application window. Wayland forwards actual key/button serials and focus/seat changes.
- `tests/unit/os/`: five asynchronous queue cases, six Unicode/chunk/conversion cases, six Linux pipe cases, and isolated X11 integration cases. The X11 cases run only through an explicitly marked temporary Xvfb display. Backend ownership, capability limits, and Linux test commands are documented in `core/os/README.md`.

Validation for the OS increment:

- The final Windows ARM64 / Clang `opt` build of `nwb_os_tests`, Testbed, and the UI-only executable passes. All 21 clipboard cases, five paint cases, nine skin cases, and 319 GPU task cases pass. Both GPU-validation UI pixel smoke tests also pass after the shared Frame/OS changes.
- Actual Linux x86_64 target syntax checks pass for 14 production and five test translation units: 19 with Wayland/primary selection, 19 with Wayland without primary selection, and 16 with X11 only, for 54 successful checks. The disposable harness uses genuine libc/libstdc++/Linux/X11/Wayland headers and generated protocol headers. Saved source/header hashes match the final files; command lines, package/source hashes, and logs are retained under `__artifacts/custom_ui/linux_sysroot/`. This does not qualify Linux ARM64 or linking.
- Linux-only pipe and X11 tests are provided for execution on Linux. This Windows host has no installed WSL, container runtime, or Linux compositor, so live X11/Wayland clipboard behavior and Linux linking remain unqualified.

The interactive toolkit increment below adds stable IDs/state, layout, focus/hit testing and basic controls in their own `impl/ui/` domains. IME remains OS work for both Win32 and Linux, borrowed through the adapter. The M3 increment below adds window/separator parity and retires the ImGui runtime, assets, build target, and vendor sources.

Build an engine-owned UI toolkit under `impl/ui/`, keep `impl/ecs_ui/` as the ECS/runtime adapter, and render UI into a transparent texture through the existing GPU task graph. Join that texture with the scene in the final output pass, after scene display mapping and before output encoding. IME, clipboard/native selection exchange, and other window-system services belong to a separate OS feature layer that `ecs_ui` borrows. Controls use a customizable skin texture plus atlas information authored in an `.nwb` file; the engine ships one default texture/atlas pair. Remove ImGui once current application behavior is covered; continue the larger widget library independently.

Implemented in the font/text foundation increment:

- `impl/assets_font/` and `impl/assets/ui/fonts/default/`: typed static SFNT font assets with strict cook/load validation, original source-byte ownership, pinned Noto Latin/Korean defaults, and retained licences. FreeType 2.14.3 and HarfBuzz 14.5.0 are explicit static packages independent of ImGui and platform text APIs.
- `impl/ui/text/`: separately owned native font versions, `interface ITextShaper`, HarfBuzz shaping/fallback, failure-atomic multiline label layout, advance and ink measurement, UTF-8 source clusters, hit testing, and available caret edges. Direction, script and language are explicit; paragraph bidi, automatic wrapping, grapheme editing, selection/undo, and IME remain future work.
- `impl/ui/widgets/label*`: a passive label with owned text/style and cached logical layout, invalidated by text/style or text-service identity/font-generation changes. This does not implement button/edit/combo interaction.
- `impl/ui/paint_glyph*` and the GPU glyph resource domain: separate linear R8 coverage pages, append-only coordinates, immutable page versions, adjacent glyph batching, bounded GPU caching, retained upload readiness, and graph dependencies. Snapshot-local page identities keep different Latin/Korean/fixture resources distinct. Each glyph multiplies premultiplied color and alpha by coverage once.
- `impl/ecs_ui/layer_system_fonts.cpp`: explicit project-supplied typed font stack, loaded once using caller scratch storage and copied into owned CPU font versions. Paint callbacks borrow the text service alongside paint/display/OS services.
- Testbed's separate skin-preview class now paints persistent Latin and Korean labels. The UI-only smoke adds ligature/combining-mark text, Korean fallback, clipped text, and independent zero/half/full coverage samples.

Validation for this increment on Windows ARM64 / Clang, `opt` configuration:

- Production libraries, the font asset tests, UI tests, ECS graphics tests, UI-only executable, and Testbed build. Asset pipelines cook/gather 134 Testbed assets and 117 engine-only smoke assets, including both font defaults and the recooked glyph shader.
- All 26 UI checks pass: the five original paint checks, five glyph snapshot checks, eight layout checks, and eight real-font/shaping/atlas checks. Eight font asset checks and eight related composition/glyph shader checks also pass.
- Both GPU-validation UI smoke tests pass. The acquired-backbuffer capture validates 19 numeric probes at 960 x 540; resize validates 38 probes across 960 x 540 and 800 x 600. Each capture additionally checks Latin/Korean/clipped ink, varied antialiased edges, and absence of ink beyond the text clip. Maximum channel error is two byte values.
- A GPU-validation Testbed capture succeeds at 1280 x 900 and after a 901 x 607 resize, showing the custom labels alongside the existing scene and ImGui overlay. Captures were visually inspected. Live HDR remains unqualified on this host.
- All 22 affected font/text/paint/ECS/GPU production translation units and four new test translation units pass actual Linux x86_64 syntax checks with genuine native headers and the Linux sysroot. This host has no Linux runtime; this is not a Linux link or native-execution claim.
- The known unrelated stale surfel normal source-text expectation recorded above is unchanged; this increment runs the affected ECS composition/glyph checks rather than asserting that the whole ECS suite passes.

Implemented in the offline SDF increment:

- `utilities/font_atlas/`: a portable offline C++ utility and repository launcher, with separate source admission, SDF rendering, deterministic shelf packing, positioning-table export, and atomic package publication. The exact pinned FreeType SDF modules are restored.
- `impl/assets_font_atlas/`: typed `FontAtlas` assets, strict readable `.nwb` schema, raw linear RGBA payloads, content hashes, bounded shared validation, explicit little-endian binary codec, normal cooker registration, and exact source-font matching. Four independent scalar distance pages use R/G/B/A; alpha is another distance page.
- Kerning metadata preserves the original `kern`, `GPOS`, and `GDEF` table bytes, including class matrices, script/feature scope, and lookup order without quadratic pair expansion. HarfBuzz still shapes the original source font; exported data is never applied a second time.
- `impl/ui/text/baked_atlas.*`, `sdf_page.*`, and `service_paint.cpp`: copied immutable atlas versions, actual shaped-face/glyph lookup, padded geometry, mixed SDF/native coverage painting, and snapshot lifetime after asset release or replacement. Optional unavailable/mismatched atlases retain the same face's native coverage path.
- `impl/ui/paint_images.cpp`, `paint_sdf.cpp`, and `gpu/renderer_sdf_resources.*` / `renderer_image_cache.cpp`: atomic mixed-image admission, channel-aware adjacent batching, linear RGBA uploads, descriptors prepared before draw recording, accepted upload readiness retained across descriptor retries, exact graph imports, shared bounded-cache capacity, completed-frame retirement before descriptor allocation, and resources retained through the final consumer.
- Engine-default Latin and Korean atlas assets are generated at 32 ppem/spread 8 and selected explicitly by Testbed and the UI-only smoke. They contain 3,748 and 24,964 glyph records respectively, with 8 MiB and 64 MiB of raw image payloads. Source font bytes/licences remain available.

The qualified policy uses SDF at 0.5..1.5 times the bake ppem in physical pixels, with native coverage outside it. For the default 32-ppem atlases, that is 16..48 physical pixels per em. Thirty-six Latin A/o/e and Korean Hangul comparisons against supersampled native rendering have maximum edge displacement one pixel and maximum mean coverage error 0.0068433573. Larger candidate magnifications failed the one-pixel edge gate at sharp corners, so this increment does not claim arbitrary zoom quality. The implemented format, utility, and runtime contract are documented in [font_atlas_plan.md](font_atlas_plan.md).

Validation for this increment on Windows ARM64 / Clang, `opt` configuration:

- Production libraries, utility, asset builder, both sample applications, and affected test targets build. The regular pipeline cooks 136 Testbed assets; the engine-only smoke pipeline cooks 119 assets, including both atlases and the updated shader.
- All 42 UI tests, 12 atlas asset tests, six bake tests, eight source-font asset tests, and 12 affected composition/glyph/color/shader checks pass. Ten real CLI integration cases cover reproducibility, complete font generation, exact positioning bytes, malformed inputs, capacity/overwrite failures, and preservation of previous output. The separate quality test passes 36 comparisons and verifies attachment of both default atlases; all 46 existing repository launcher tests also pass.
- Both GPU-validation UI backbuffer/resize smoke tests pass 75 numeric probes over three captures, plus twelve text checks for ink, clipping, and antialiased edges. Maximum observed channel error is two byte values. A visually inspected Testbed capture passes after resizing to 901 x 607.
- All 55 unique affected production and test translation units pass Linux x86_64 syntax checks using actual Linux headers. Command lines, logs, and final source hashes are retained under `__artifacts/custom_ui/linux_sysroot/`. Native Linux linking, baking equivalence, and GPU execution remain unqualified on this Windows host.

This completes the additional font-image path in M2. Interactive controls, OS IME, native Linux execution, live HDR qualification, and ImGui retirement retain their separate milestone gates.

## Interactive foundation increment

Merged `main` through `99ff9fea4`, preserving its struct-padding changes. The CPU toolkit now has scoped
stable widget/root IDs, typed retained declaration lifetimes, bounded row/column/overlay measure and
arrange, fixed/content/weighted-stretch sizing, ancestor clipping, committed-layout hit testing, pointer
ownership/capture, Tab/Shift+Tab focus traversal and initial Enter/Space activation. Panels, labels, buttons
and controlled checkboxes use semantic skin parts; state-family padding/minimum sizes avoid hover/press
layout shifts. Each concern has a separate source domain. Frozen GPU paint still owns values/resources,
with no widget callbacks, host model pointers or ECS/native dependencies.

The ECS adapter scopes by full generational EntityID and sorts visible roots by explicit order then ID.
It validates root lifetime while GPU work is pending, consumes queued lifetime-stamped actions once, and
publishes candidate input geometry only when matching final GPU output and the exact native presentation
receipt are accepted. Rejected/unknown later acquisitions cannot publish the candidate; matching rejection
clears it before rebuilding. Native acceptance is a queueing boundary, not monitor scan-out completion.

Win32, X11 and Wayland forward actual native focus changes. Focus broadcasts and key/button releases visit
all current handlers, including mutation-safe dispatch. The custom adapter handles first-click ownership,
keeps held pointer/key sequences with their owner through release, and delegates the legacy overlay through
one prioritized route. Legacy hit regions are copied from completed visible windows and use logical DPI
coordinates. The Testbed camera remains behind UI input and clears held state on focus loss.

Testbed includes an interactive skinned gallery while retaining existing ImGui controls. The UI-only fixture
adds native first-click, keyboard repeat/navigation, checkbox/disabled, drag-out/release, focus-loss and resize
coverage using displayed model markers and exact action logs. CPU regression coverage extends retained
identity, layout geometry, router state and builder/skin behavior.

Validation for the interactive increment on Windows ARM64 / Clang, `opt` configuration:

- The UI/input/graphics-presentation/GPU-task/OS CTest suites pass: 94 UI, seven input, 51 presentation, 319 enabled GPU-task, and 21 OS checks (492 enabled checks total; 90 GPU-task checks remain disabled).
- All four GPU-validation smoke tests pass: completed framebuffer, resize, native control interaction, and the scene/Testbed capture. The interaction fixture passes all 14 displayed-state gates and nine exact action-log checks. The 1280 x 900 Testbed capture was visually inspected and shows the interactive custom panel alongside the existing ImGui overlay.
- Linux x86_64 syntax checks pass for 92 distinct production/test translation units using genuine native headers. All 92 per-TU and 200 source/header SHA256 observations remain stable during the run. This host does not qualify Linux linking or native X11/Wayland execution; the two existing Wayland listener-tail initializer warnings remain.
- Authored source checks cover CRLF, UTF-8 without BOM, project banners/separators, exact EOF, definition order, domain separation, and whitespace. Newly authored source files stay below 500 lines.

The Windows input harness uses posted native messages and a real Shift modifier, without physical pointer-grab qualification. Native pointer-leave/capture-loss notifications and exact character/IME event provenance remain follow-up OS/input contracts.

This completes the initial interactive foundation portion of M1 and begins basic controls from M4.
ImGui dependency removal still needs movable/window/separator parity and Testbed migration in M3.
IME for Win32/Linux, edit boxes, combo boxes, popups/modals, virtualized lists, selection, docking, broader
Unicode editing and native Linux/HDR runtime qualification retain their later milestone gates.

## Window parity and ImGui retirement increment

Merged `main` through `c962f5c23` before implementation. Window chrome, behavior, layout, painting, builder integration, and separators are separate sources under `impl/ui/widgets/`. A host-owned `WindowState` retains bounds and collapse state; `WindowOptions` supplies first-use geometry, optional first-use content sizing, minimum dimensions, and movement/resizing/collapse policy. Every admitted window pairs `beginWindow()` with `endWindow()`, including collapsed windows. Internal chrome IDs belong to the window scope; normal controls keep their existing declaration lifetime and keyboard focus rules.

Pointer gestures are bounded immutable records of the committed displayed target and full reference geometry. Complete press/move/release batches survive until the next callback, active motion is coalesced once per press, and new drags start from their displayed geometry even when the host model contains an unaccepted candidate. Native pointer leave retains logical capture. Capture loss cancels the unfinished pointer sequence without erasing completed actions or keyboard focus/ownership. Focus loss performs full interaction cancellation.

Win32 forwards mouse leave, capture change, and cancel mode. X11 forwards leave/unmap/destruction without treating an ordinary implicit-grab release as cancellation. Wayland forwards real leave, seat/pointer capability loss, and close through the existing event boundary. Core input broadcasts these lifecycle notifications to every current handler with the same mutation-safe dispatch used for focus. The ECS adapter owns custom/scene routing and borrows native services; its callbacks and live host state remain outside GPU snapshots.

Testbed's original window, two labels, and separator use the custom builder. The application has one `UiPaintComponent` and one `UiLayerSystem`, with the custom window above the preview gallery. Camera input respects custom target and held-input ownership; capture cancellation only clears an active mouse-look gesture.

An isolated replacement skin supplies a separately converted amber/green texture and an atlas with remapped UVs, different content padding, and changed title height. The same window/controls and native interaction sequence run against default and replacement skins; capture probes compare their actual sampled colors and chrome dimensions. The fixture pair stays under `tests/smoke/ui_layer/assets/`, with a deterministic generator and no checked-in source PNG.

The legacy ECS adapter/callbacks, ImGui shaders and metadata, CMake dependency, runtime-list entry, and vendor directory are removed. Generic task graph presentation, acceptance, recovery, output-layer, and timing contracts remain. Both scene and standalone paths use the existing independent UI GPU layer and exact accepted presentation receipt.

Validation for the M3 increment on Windows ARM64 / Clang, `opt` configuration:

- A fresh CMake build directory with the ImGui directory absent builds the custom toolkit, ECS adapter, Testbed, standalone UI executable, and affected unit targets. Fresh output/cache paths cook and gather 134 Testbed assets and 119 standalone assets (117 engine plus the replacement atlas/texture). No production/build/asset source, compile command, or cooked input manifest retains an ImGui dependency.
- All five core suites pass: 143 UI, 11 input, 51 graphics presentation, 319 enabled GPU task, and 21 OS checks, for 545 enabled checks. The 90 pre-existing GPU task checks remain disabled. Thirteen targeted ECS output-layer, SDR/HDR color, glyph, and timing checks also pass; the unrelated stale surfel-normal source check noted above is outside this filtered run.
- GPU-validation standalone framebuffer, resize, basic interaction, default-window, and replacement-window smoke tests pass. The two window runs each verify 20 displayed stages and six exact action logs; the basic control run verifies 14 stages and nine actions. Default/replacement skin colors and geometry, collapse/body inactivity, minimum resize, drag completion, locking, capture/focus loss, pointer leave, and first interactions after resize are checked against real displayed pixels.
- A scene/Testbed capture with GPU validation passes at a 1280x900 original client and after a 901x607 resize. The original labels/window use the custom font/skin path, with an explicit one-logical-unit separator. Testbed and both skin captures were visually inspected.
- Linux x86_64 syntax validation passes 119 checks over 97 distinct production/test translation units: 97 with primary selection, 12 without it, and 10 X11-only. All 725 source/header hashes per mode remain unchanged, and final current-hash comparison has zero mismatches. Commands and logs remain under `__artifacts/custom_ui/ui_retirement_linux/`. The two existing newer-Wayland listener-tail warning types remain; newly introduced warnings are resolved.
- New/modified authored C++ and asset text pass UTF-8, CRLF, banner/separator gaps, exact source EOF, and whitespace checks. Production window concerns are separate sources of at most 160 lines; the largest newly authored C++ file is the 604-line gesture test source.

M3 removes the ImGui dependency and demonstrates replacement texture/atlas skins. Win32 native message delivery/rendering is qualified here. Linux linking/native X11/Wayland execution, physical pointer grabs, and live HDR remain unqualified on this Windows host. M4/M5 continue the requested edit box, combo box, list, and OS IME work.

## Text-edit and OS IME foundation increment

This increment establishes M4's editing state and native input boundaries. It does not yet add a visual Builder edit box or complete M4's exit gates.

- `impl/ui/edit/`: an arena-owned single-line UTF8 edit model, byte selections on Unicode 17 extended grapheme boundaries, selection/navigation/deletion, bounded copied undo/redo, external value replacement, and transient composition. Sources are split into model, navigation, history, composition and Unicode segmentation/property domains. Surrounding-text deletion preserves the original selection in undo. Word navigation uses documented whitespace/punctuation/text runs.
- `impl/ui/edit/unicode/`: generated, pinned Unicode 17 grapheme/Indic/pictograph tables, the Unicode license and a SHA256 source manifest. `generate_tables.py` reproduces the split tables and the official 766-case GraphemeBreakTest fixture using pinned inputs or an offline source directory. Grapheme tests include Indic conjuncts, Korean jamo, combining sequences, emoji ZWJ/modifiers and regional-indicator pairing.
- `core/os/text_input*`: `interface ITextInputService` and a bounded owning event queue for one event-thread session. Tokens include service/lifetime generation; all text and UTF8 byte ranges are copied/validated. Preedit may coalesce between commit barriers. Focus loss or overflow leaves an explicit terminal cancellation. Published surrounding revisions fence deletion, and native update cancellation is reported by the update result.
- `core/os/win32/`: separate IMM32 session, message, composition and native-context sources. UTF16 decoding and its pending surrogate belong to the native service for active editor commits and inactive scene fallback, with resets on focus and session changes. IME result text commits once; consumed composition/IME-char messages bypass duplicate default synthesis. Native composition/candidate geometry follows the host caret rectangle. Surrounding/deletion capabilities remain unsupported by this backend.
- `core/os/linux/x11/`: owned XIM/XIC lifetimes, `XFilterEvent`/`Xutf8LookupString`, callback preedit and character-to-UTF8-byte conversion, insertion-caret updates and focus cancellation. Filtered physical keys retain normal key ownership while text delivery follows XIM. Unavailable XIM/preedit styles report actual capabilities.
- `core/os/linux/wayland/`: optional text-input-v3 manager/device and protocol generation, selected-seat/surface/keyboard focus, pixel-to-surface caret conversion, atomic `done(serial)` batches, and exact native surrounding provenance. Without build/compositor protocol support the service accepts direct xkb commits and advertises unavailable preedit/surrounding capabilities. Delayed current-session commits remain valid; stale, unavailable, or nonzero deletion against a selected surrounding range terminates the session. Input-method feedback preserves its native change cause, including deferred updates.
- `core/frame/` and `loader/`: Frame creates and tears down the OS service around project borrowers, forwards native focus/messages, and passes a required borrowed `ProjectRuntimeContext.textInput`. `UiLayerSystem` and `UiPaintContext` borrow it alongside clipboard/native-selection services.
- `impl/ecs_ui/text_edit_session*`: a separate host-call adapter that applies owned events to a temporarily lent model. Widget/declaration/model generations and copied text/selection/composition reject old-owner or externally changed state. Preedit stays out of application text/history. Destruction releases the OS token without borrowing a model; explicit end with the old model also clears its transient preedit.

Validation for this increment on Windows ARM64 / Clang 22.1.4, `dbg` configuration:

- The coordinated build of the production UI/OS/ECS libraries, Testbed, UI smoke executable, and affected unit targets passes. Runtime asset cooking gathers 134 Testbed assets and 119 engine-only smoke assets.
- All 338 unit tests pass: 177 UI, 16 ECS UI, 83 OS, 11 input, and 51 graphics-presentation cases, with no failures or disabled cases in these suites. This includes 112 new tests for editing, text-input services and their ECS adapter; one grapheme case additionally checks all 766 official Unicode 17 conformance rows. The OS suite includes 20 actual hidden-HWND native cases and 21 portable Linux backend-model cases.
- All five GPU-validation UI smoke tests pass: completed-backbuffer painting (25 probes), resize (50 probes across two captures), native interaction (14 observed states), windows (20 observed states), and alternate skin/windows (20 observed states). The GPU-validation Testbed startup/shutdown capture also passes and is visually inspected at 1280 x 900.
- Linux x86_64 target syntax qualification passes 201 C++ checks: 124 with Wayland/text-input-v3/primary selection, 27 without primary selection, 27 without text-input-v3, and 23 with X11 only. The protocol header/private C source are generated by the actual upstream Wayland scanner; the generated C also passes a target syntax check. Ten affected C++ checks are repeated after final formatting changes. Genuine Linux libc/libstdc++/X11/Wayland headers, commands, source hashes, generation provenance and logs are retained under `__artifacts/custom_ui/`.
- Modified/new authored C++ sources pass UTF-8, CRLF, exact separator/EOF and whitespace checks. New production translation units are split by domain and remain below 650 lines; the existing Frame Wayland file receives only native forwarding/lifetime hooks.

The visual edit-box increment below binds this session adapter to committed focus/identity, routes editing and clipboard commands, and paints text, selection, caret and preedit through the existing frozen GPU layer. Multiline/numeric editors and compound combo/list controls follow after the single-line editor is qualified. Live Korean IME composition and Linux linking/native compositor execution require their respective target environments; deterministic event/Unicode checks alone do not qualify them.

## Single-file font atlas increment

The utility now publishes one authoring schema 2 `.nwb` containing glyph metadata, all RGBA groups, and the original OpenType positioning tables. Each decoded payload is compressed into a deterministic Zstd frame and embedded as canonical base64 chunks. Decoded byte counts and SHA-256 remain integrity checks; generated packages no longer refer to hash-named payload files. Native source fonts remain separate typed `Font` assets for shaping.

- `global/base64.h` owns bounded canonical encoding/decoding, including failure preservation and aliased input/output.
- `impl/assets_font_atlas/source_payload.*` owns the shared source compression contract. `cook_metadata_payload.cpp` validates chunks and frame limits before publishing decoded data. The cooker retains legacy authoring schema 1 imports and the runtime `FTA1` format remains version 1.
- `utilities/font_atlas/asset_metadata.*` builds the complete document separately from exclusive temporary-file staging and final publication in `asset_writer.cpp`. Both Win32 and Linux file paths stage, flush, verify and replace one requested atlas. Unrelated output files are preserved.
- The default Latin and Korean atlases are regenerated into one `.nwb` each, and their ten old sidecars are removed. Independent decoding verifies unchanged source-font hashes, 3,748 / 24,964 glyph records, 2 / 4 RGBA groups, and exact original `GPOS`/`GDEF` bytes. The new files contain 5,176,115 / 51,901,890 bytes, compared with 9,094,378 / 71,528,974 bytes for their previous metadata-plus-sidecar packages.

Validation on Windows ARM64 / Clang 22.1.4, `dbg` configuration:

- Seven base64, eighteen font-atlas asset, and six bake tests pass. Asset coverage exercises deterministic compression, corrupt or unsupported frames, malformed chunk layout, bounded admission, isolated-file cooking, and preservation on failure.
- The eleven-case CLI integration suite passes with the real utility and importer. It verifies single-file publication and relocation into an otherwise empty directory, exact decoded texture/table hashes, all-glyph coverage, channel packing, repeated byte-identical output, overwrite behavior, failed bakes, and occupied temporary-file preservation.
- The production cook/gather passes for 134 Testbed assets and 119 engine-only smoke assets after sidecar removal. All 177 UI tests and the default-atlas SDF quality case pass, including its 36 Latin/Korean comparisons against supersampled native outlines across the qualified zoom/DPI range.
- GPU-validation framebuffer, resize and Testbed capture tests pass. Standalone captures pass 25 / 50 numeric probes; current standalone and 1280 x 900 Testbed text captures are visually inspected.
- Sixteen affected C++ translation units pass Linux x86_64 syntax checks using genuine Linux libc/libstdc++ headers and the existing vendored dependency headers, with no final diagnostics. Native Linux linking, execution and cross-platform bake byte equivalence remain unqualified on this Windows host.
- All fourteen changed/new authored C++ files pass UTF-8, CRLF, exact separator/EOF and whitespace checks. The largest new translation unit is the 357-line source-import test; production sources are split by compression, import, metadata and publication domains.

This packaging increment changes only font authoring transport. The visual single-line editor below consumes the same immutable runtime font pages and shaping assets.

## Visual single-line edit-box increment

This increment connects the existing edit model and OS feature layer to a reusable visual Builder control. It advances M4; the popup increment below builds on its focus and native preedit behavior while list/combo and multiline/numeric editor milestones remain separate.

- `impl/ui/widgets/edit_box_state.h` supplies `EditBoxOptions`, application-owned `EditBoxState`, `EditBoxResult` and `interface IEditBoxHost`. `Builder::editBox(key, model, state, options)` borrows its model/state through the matching panel/window end. Sizing, enabled/read-only mode, text/selection changes, submit/cancel and focus are explicit. An accepted external `setText()` advances an external revision even when its bytes are unchanged; an automatic model lifetime generation fences replacement at a reused address.
- `impl/ui/widgets/edit_box_layout.cpp`, `edit_box_paint.cpp` and `builder_edit_box.cpp` separate copied text/font geometry, clipped skin/selection/caret/preedit painting, and declarations. Grapheme caret positions interpolate inside LTR ligatures. Horizontal scrolling keeps the caret visible, and the caret width is one physical pixel after display scaling. A declaration's state exposes prepared placement; pointer/IME admission uses the adapter's separately retained displayed placement.
- `impl/ui/edit/commands.*` provides grapheme and word-run movement/deletion, Shift selection, Home/End, select-all, clipboard intents, history, and submit/cancel. Active preedit keeps editing keys and Enter with the native input method; Escape cancels preedit before widget cancellation. `single_line_text.*` provides a bounded clipboard insertion policy: line breaks/tabs become spaces, other text stays UTF8, and invalid/control/oversized values preserve the model.
- `impl/ecs_ui/edit_box_host.cpp`, `edit_box_input.cpp` and `edit_box_model.cpp` own entry/frame/geometry state, ordered input collection, and temporarily lent model operations. Native events are collected before queued key/pointer intents. Owned snapshots and events carry widget/declaration/model generations, revisions and selections; the host retains no model pointer between calls. Hidden/removed roots and controls, read-only/disabled changes, focus loss, external reset, resize/DPI/device boundaries and teardown fence native sessions and late work.
- The host publishes its candidate caret stops only for the exact accepted UI paint/presentation generation. Pointer selection uses that displayed text geometry and verifies its model/external revision; native caret rectangles convert the same logical placement into client pixels. A queued commit/arrow/commit sequence is applied in order without borrowing live application state from native callbacks or GPU tasks.
- `impl/ecs_ui/edit_clipboard_controller.*` borrows queued OS clipboard/native-selection interfaces. It owns copied request state and verifies the live owner/model before cut/paste mutation. Cut deletes after successful publication, paste performs one validated replacement/history transaction, and stale/cancelled/unsupported delivery cannot mutate another editor. `clipboard_publications.*` separately owns immutable Copy requests that can complete after editing, focus transfer or widget removal. Read-only fields permit selection and copy. Available primary-selection publication remains capability-aware; middle-button paste replaces the already focused control's current selection without moving focus or caret to the pointer.
- Testbed adds a separate edit-gallery class with editable Latin/Korean and read-only copy fields. The standalone fixture adds native editing, focus/replacement/visibility boundaries and resize/scroll scenarios. Numeric model/sequence markers gate the actual presented capture alongside skin, selection, caret and clip pixels. Fixture native input, metadata parsing and pixel checking remain separate sources.

The OS owns IMM32/XIM/text-input-v3 and candidate-window behavior, while `ecs_ui` borrows those services and the toolkit paints transient preedit independently of committed application text/history. The current editor supports one LTR line. Paragraph bidi/RTL editing, wrapping, numeric/multiline editing, compound controls, live Korean IME qualification and native Linux compositor execution retain explicit follow-up scope. Synthetic Win32 character commits and X11 ASCII event tests do not establish live IME behavior.

Validation for this increment on Windows ARM64 / Clang 22.1.4, `dbg` configuration:

- The coordinated native build of the production libraries, Testbed and standalone UI smoke executable passes.
- All 402 unit tests pass: 198 UI, 59 ECS UI, 83 OS, 11 input and 51 graphics-presentation cases. Coverage includes visual LTR/grapheme geometry, edit command policy, normalized paste, immutable Copy lifetime, fenced asynchronous cut/paste, ordered native/keyboard input, and external replacement.
- `nwb_ui_layer_edit_smoke` passes all 26 native/GPU displayed-state gates with GPU validation. Matching numeric model/sequence pixels, skin, selection, caret and clip probes verify Unicode editing, clipboard/history, read-only/focus/hidden/replacement boundaries, interleaved commit/arrow/commit, resize and horizontal scroll. The Win32 path uses synthetic character messages and actual Control/Shift modifiers; it does not qualify live IME composition or physical pointer grabs.
- All seven GPU-validation smoke tests pass: the new editor sequence, existing framebuffer, resize, basic interaction, default-window, alternate-skin/window and scene/Testbed captures. Visual inspection confirms the selected Korean grapheme and the new editable/read-only Testbed gallery.
- Linux x86_64 target syntax checks pass for 39 affected production/test translation units using genuine Linux headers. Commands, source hashes and logs remain under `__artifacts/custom_ui/edit_box_linux/`. Native Linux linking, compositor execution and live XIM/text-input-v3 behavior remain unqualified on this Windows host.

## Popup and modal increment

This increment adds the popup infrastructure needed by later combo boxes and lists. It advances M4 without completing scrolling/virtualized selection, compound controls or the full M4 exit gates.

- `impl/ui/widgets/popup.h`, `popup_state.cpp` and `popup_layout.cpp` separate application-owned state from logical placement. `PopupState` is noncopyable/nonmovable and has an instance lifetime plus a new open generation for each opening. `PopupOptions` supplies the anchor, desired size, Below/Above/Right/Left/Center placement, gap, modal mode and outside/Escape/autofocus policies. Anchored placement flips on its requested axis when the opposite side has more room, clamps to the logical viewport, and shrinks oversized dimensions to that viewport.
- `builder_popup.cpp` and `popup_paint.cpp` separate scoped declarations from skin/backdrop/control painting. Declare a popup after existing panels/windows have ended. Only a `true` `beginPopup()` requires the matching `endPopup()`, and the application state must live through that end. Ordinary label/button/checkbox/edit-box declarations work inside the popup. Closing/reopening during the body fences later child actions and model loans, and suppresses the old opening's buffered Builder paint and targets. Direct low-level paint already emitted into the overlay is not rolled back. One Builder scope is open at a time; nested popup layout is not supported.
- `Builder::popupStyle()` selects `popup.normal`, falling back to `panel.normal` in the same chosen skin, and supplies padding plus a linear backdrop color with straight alpha. Paint emission applies the existing premultiplied vertex contract. Layout combines style padding with the resolved atlas region's padding. The engine default and replacement skins keep their existing texture/atlas pairs; no popup-specific texture or asset format is introduced.
- `paint_overlay.cpp` adds balanced nonnested overlay scopes. Base commands have layer zero; popup commands have increasing nonzero layers and a viewport clip independent of a previous parent clip. Freezing orders command ranges by layer and preserves their order within a layer without rewriting owned geometry or image bindings. Popup paint therefore remains above later ordinary host roots. Modal backdrops dim the whole viewport before the popup at that layer. These are command layers inside the existing independent GPU UI texture and final compositor, not separate per-popup render targets or GPU tasks.
- `context_popup.cpp` and `input/router_popup*` retain copied popup scopes and lifetime tokens. A token includes widget identity, declaration lifetime, state instance and open generation. Hit targets, queued activations and capture also carry their exact popup token. Candidate scopes and geometry publish only with the exact accepted UI paint/presentation generation; a replacement/reopening fences old tokens immediately, and queued old-scope actions cannot activate a new opening.
- The top accepted popup owns keyboard traversal and pointer routing. Autofocus selects its first eligible child on acceptance, Tab/Shift+Tab wrap within its children, and explicit accepted close restores the previous focus only if that target and declaration remain eligible. Declaration/root retirement removes ownership immediately and may restore an eligible lower target at retirement. Return chains survive scope insertion/removal/reordering. Outside clicks and Escape obey the popup policy while retaining UI ownership through the held sequence and release, so the dismissing click cannot activate a lower control or scene input. A non-dismissible outside click is consumed while leaving the popup open. Native window focus loss closes popups and cancels restoration into an unfocused window. A focus-loss generation captured at preparation also fences delayed popup acceptance even if focus returns before commit; gain alone cannot restore a closing scope.
- The edit-box host keeps Escape with active preedit first: that press cancels transient composition while retaining the editor/popup. A subsequent editor cancel asks the router to dismiss the top popup; ordinary editors outside popups retain their existing cancellation behavior. Host entries and candidate/displayed geometry carry popup tokens, fencing old native/key intentions and cut/paste completions when the same model is reused after reopening. Native text/clipboard/selection remain OS services, and popup scopes retain no application model/state pointers between declarations or in GPU work.
- Testbed adds a separate popup-gallery class and a Menu trigger in its existing action row. The standalone fixture places popup content over a second higher-order host root and uses numeric model/sequence markers, skin pixels and modal-dimming probes. Separate native orchestration and metadata/pixel helpers run the same behavior with default and alternate atlases. Existing scene selectors are fenced so each smoke chooses one scene explicitly.

Validation for this increment on Windows ARM64 / Clang 22.1.4, `dbg` configuration:

- The native build of the production libraries, Testbed and standalone UI smoke executable passes.
- All 489 unit tests pass: 278 UI, 66 ECS UI, 83 OS, 11 input and 51 graphics-presentation cases. New coverage includes placement, layer/clip ordering, atomic scope publication, focus return chains, held-sequence and old-epoch fencing, body close/reopen, pending native-focus loss, borrowed session handoff, transient preedit Escape and stale paste/native/key intentions.
- All nine GPU-validation smoke tests pass: default/alternate popup sequences plus editor, framebuffer, resize, basic interaction, default/alternate windows and scene/Testbed capture. Each popup sequence passes 31 displayed-state gates using matching numeric model/sequence pixels, popup skin, late-root overlap and modal-dimming probes. Visual inspection confirms both skin families and modal composition.
- Genuine Linux x86_64 syntax/type checks pass for 90 distinct production/test translation units, including X11 and Wayland clipboard/text-input backends. The report records per-TU commands/hashes and compiler dependency files. All 9,815 source/header/include inputs and 1,038 actual dependencies remained unchanged, with no missing dependency hashes or compiler failures. Results are under `__artifacts/custom_ui/popups_linux/`.
- All 45 changed C++ source/header files pass UTF-8 without BOM, CRLF, banner/separator and exact EOF checks; the largest is 636 lines. `git diff --check` and changed smoke Python syntax checks pass.

The native Windows harness uses posted Win32 messages and native modifier state; synthetic text bypasses live IME composition. The X11 harness is provided separately. Linux syntax/type checks do not qualify native linking, X11/Wayland compositor execution or live XIM/text-input-v3 behavior. Physical pointer grabs, live HDR, nested popup layouts, tooltips/context menus, scrolling lists and combo boxes retain separate scope.

## Combo selection increment

This increment composes the keyed list and popup infrastructure into `Builder::comboBox`. The component borrows an application-owned `ComboState` and `interface IListDataSource` through the containing panel/window end. Declaration consumes the previous accepted field/popup/list intentions and returns immediate `committed`/`selectionChanged` results. Scope-end layout anchors and paints the internal popup above ordinary content, using the existing independent UI GPU layer and final framebuffer composition.

- The trigger supports pointer, Enter/Space and Up/Down opening. The popup previews enabled stable keys with Arrow/Page/Home/End navigation; only Enter/Space or a row pointer release commits. Escape/outside/native focus loss cancels preview, and the dismissing pointer sequence cannot activate content underneath.
- The selected row is ensured visible when opening. Wheel and accepted thumb gestures reuse list scrolling without committing selection. The arithmetic 100,000-row fixture shapes only visible rows and the selected field label, with no dataset-sized widget allocation.
- Source revisions fence stale row/navigation/wheel/capture intentions while preserving valid keys. Source replacement clears selection/scroll and closes. Explicit model changes renew input lifetime; omitted/recreated or reparented declarations retire the old popup on rebinding. Held owners cannot transfer to the new field/popup lifetime. A state instance may belong to only one combo per frame across panels/roots.
- Deferred field/popup work retains the borrowed models through the matching end and rejects detected state/source changes. Callback reentry preserves explicit application mutation. Sources must remain stable for that loan and must not mutate another borrowed dataset from a query callback. Frozen snapshots and accepted targets contain copied IDs, keys, tokens and geometry.
- `combo.normal/hover/open/focused/disabled` and `combo.arrow` are semantic skin roles. Both atlas files alias existing texture artwork for the new states; popup/list/scrollbar parts reuse their existing roles. Builder admission, lifetime/state, keyed behavior, input and painting live in separate domain sources, with reusable list painting and scrollbar geometry.

Qualification on Windows ARM64 / Clang 22.1.4, `dbg`: all 674 native unit tests pass (UI 463, ECS UI 66, OS 83, input 11, graphics presentation 51), including 67 new combo tests. All 13 GPU smoke tests pass with graphics validation, including 64 matching displayed/native/pixel gates for each combo skin. The 100,000-row combo fixture reads at most 10 labels per frame. Genuine Linux x86_64 syntax/type checks pass for 127 translation units covering shared UI plus X11/Wayland input, text-input-v3 and primary selection; 9,857 source/include inputs and all 1,120 actual compiler dependencies remain unchanged through the run. These checks do not establish Linux native linking/compositor execution, physical pointer grabs or live IME. All 28 changed C++ files pass UTF-8, CRLF, banner/separator, exact EOF and source-size checks; the largest is 662 lines. The default generator and checked-in atlas agree by semantic region, and both skins resolve all five combo states using existing texture tiles.

The native combo fixture provides default/replacement-skin pixel checks, accepted geometry and model markers, preview/commit/cancel, disabled/empty data, reorder/removal, omission/held owners, source replacement, bottom-edge flipping and resize. Linux uses the shared control behavior with existing Win32/X11/Wayland OS input adapters; target compilation and native execution have different qualification scopes. Tab retains the popup focus trap. Searchable combos, nested user-popup composition, tooltips/context menus, numeric/multiline editors and native Linux compositor/live IME qualification remain separate work. This advances M5 without claiming all M5 exit gates complete.

## M5 searchable combo increment (2026-09-29)

Merged current `origin/main` (`e2402c958`) into `custom_ui` before this increment. `Builder::searchComboBox()` now composes the existing combo field, popup, query edit box and virtualized result list. Application-owned `SearchComboState` keeps committed selection separate from the query and preview. `interface ISearchableListDataSource` supplies a cached filtered view with original stable keys; source metadata and lookup contracts keep visible-row work bounded without scanning the dataset in the UI layer.

Committed query changes filter results while preserving a value hidden by the query. Query/source/view changes retire stale result intentions and reset preview/scroll; reopen retains the query. Disabled rows and empty results cannot commit. Typing, caret movement, selection, undo/redo, IME and clipboard use the existing OS-backed edit host. Explicit copied keyboard delegation lets arrows and page keys navigate results without moving editor focus; Enter requires a noncomposing edit submission. Preedit stays separate, and Escape cancels composition before popup dismissal. Query composition now has its own generation so deferred callbacks cannot publish stale preedit geometry or revive held navigation.

The default and replacement skins reuse the existing semantic combo, edit, popup and list parts. The Testbed gallery includes a searchable fruit selector, and the native fixture searches 100,000 arithmetic rows with cached prefix ranges. Focus, popup/source/query lifetimes, clipboard ownership and delayed GPU acceptance remain governed by the existing shared input and final UI GPU composition paths.

Qualification on Windows ARM64 / Clang 22.1.4, `dbg`: all 745 native UI/ECS-UI/OS/input/presentation tests pass, including 71 new query, keyboard-owner, Builder and queued OS-service regressions. All 483 executed ECS graphics tests also pass after the main merge; 61 existing tests in that suite remain disabled. All 15 UI/Testbed GPU smoke tests pass with graphics validation, including 43 displayed/native/pixel gates per searchable-combo skin. Both search fixtures read at most 10 row/field labels per frame over 100,000 rows. Genuine Linux x86_64 parsing/type checks pass for 147 translation units with X11/Wayland text-input-v3 and primary selection enabled; 9,873 hashed source/include inputs and all 1,148 actual dependencies remain unchanged. All 41 changed C++ files pass source-format and size checks; the largest is 615 lines and the largest production file is 399 lines.

Native Linux linking/compositor execution, physical pointer grabs and live IME qualification remain separate from cross-platform compilation and queued service-contract tests. Nested user-popup composition, tooltips/context menus and numeric/multiline editors remain later work.

## M5 tooltip and context-menu increment (2026-09-29)

`Builder::tooltip()` attaches measured, owned help text to a preceding control in the same scope. Contiguous accepted hover accrues its configurable delay; pointer holds/capture, leaving, focus loss, disabled anchors, changed lifetimes/options and higher popups suppress it. Placement flips and clamps to the viewport. The overlay adds no input target or focus scope, and resolves `tooltip.normal` with the selected skin's `panel.normal` fallback.

`Builder::contextMenu()` attaches stable-key commands to a preceding control in a panel/window. Accepted right-button, Menu and Shift+F10 triggers copy their exact anchor/control/popup lifetimes and position. Commands share the virtualized list, scrollbar and popup domains: disabled commands cannot activate, navigation changes preview, Enter/Space/click commits one key, and outside/Escape/focus-loss dismissal consumes its input sequence. Source revisions retire stale intentions and reset preview; replacement closes the menu. Shared ECS input mapping covers Win32, X11 and Wayland; IME and clipboard stay borrowed OS features.

Application list/combo/menu sources and state stay borrowed through the enclosing scope end. All deferred callbacks complete before source checks, a callback-free model epoch pass and joint loan release. Regression coverage rejects callbacks that change already painted ordinary lists or combos while preserving the application's mutation. Closing a user popup suppresses its buffered help overlay and releases unpainted loans. Domain files separate tooltip lifetime, annotation declaration/paint, menu state/input/paint and native trigger routing. The Testbed gallery and two native display fixtures cover the default and replacement atlas skins.

Qualification on Windows ARM64 / Clang 22.1.4, `dbg`: all 835 relevant native UI/ECS-UI/OS/input/presentation tests pass (619/71/83/11/51), including 90 new tooltip, trigger, Builder, deferred-loan and focus-restoration regressions. All 15 existing UI/Testbed GPU smokes and both new popup-tools smokes pass with graphics validation. Each new skin fixture completes 45 displayed/native/pixel gates; menu painting reads at most five command labels per frame. Win32 fixtures position/restore the physical cursor, prepare the raw client once, and use posted buttons/keys plus native modifiers. That evidence does not qualify physical pointer grabs or live IME. Genuine Linux x86_64 parsing/type checks pass for 164 translation units with X11/Wayland text-input-v3 and primary selection enabled; all 9,898 hashed inputs and 1,173 actual dependencies remain unchanged. All 48 changed C++ files pass source-format/size checks (largest test 640 lines; largest production source 423 lines).

The user requested sequential implementation with a commit and push for every completed step, stopping before M6 measurement. Numeric editors and the multiline content/geometry foundation are implemented. The remaining order is the visual multiline text-area control, remaining basic widgets (radio/slider/progress/image), OS qualification and the Wayland selection-exclusion transaction, then asset/render integration. Performance acquisitions and measurement-driven tuning are deferred until those steps are handled. Native Linux execution and live IME qualification require their own runtime evidence; target compilation does not establish them.

## 1. Starting point and scope

The table records the repository baseline before the migration; legacy UI paths listed here have been removed by M3.

| Baseline code | What it meant for the migration |
| --- | --- |
| `impl/ecs_ui/components.h` | `UiComponent` already hosts a visible UI callback. Keep this application/ECS integration concept and add an explicit custom UI context. |
| `impl/ecs_ui/system.h`, `system_frame.cpp` | `UiSystem` owns ImGui, input handling, an ECS update, render-pass lifecycle, and presentation contribution. Its update is currently main-thread work. Split these responsibilities by owner. |
| `impl/ecs_ui/system.cpp` | There are already graph-owned uploads, retained draw snapshots, texture imports, submission acceptance, and retry handling. Preserve these lifecycle guarantees while replacing ImGui types. |
| `core/task/gpu/presentation_contributor.h` | The current contributor is a terminal hook after scene output. The current UI also makes its uploads depend on that endpoint, preventing an independent UI branch. |
| `impl/ecs_render/deferred/task_graph_suffix_builder.cpp` | Owns scene output, optional overlay, and the final presentation endpoint. This is the integration point for joining scene and UI. |
| `impl/assets/graphics/deferred/composite_ps.slang`, `common/hdr10.slangi` | The final graphics pass already handles scene display mapping and SDR/HDR10 output encoding. UI composition must fit this color contract. |
| `CoolStuff/Testbed/runtime.cpp` | The only first-party ImGui widget caller found: one window, two labels, and a separator. Edit boxes, combo boxes, and lists are new functionality, not current migration parity. |
| `core/input/module.h`, `core/frame/` | Reuse key, committed-character, pointer, and scroll delivery. Native windows and event pumping already live in `core/frame/`; expose new OS services separately and inject borrowed interfaces into `ecs_ui`. |

Initial target: desktop UI in the existing application window, keyboard and mouse interaction, correct display scaling, and the supported SDR/HDR rendering routes. Native detached windows, docking, rich text, touch/pen, and a full accessibility platform bridge are later work. Carry semantic roles, labels, values, and keyboard actions in the widget model from the start so accessibility does not require redesigning controls.

## 2. Ownership and public API

Target layout; identity, context, layout, text, basic widgets, windows, the visual single-line editor, GPU composition, and borrowed clipboard/text-input services now exist as described above. Larger widget and window-service domains remain proposed:

```text
core/
  os/                         nwb_os: reusable OS services, no UI/ECS dependency
    text_input.h              composition sessions/events and caret geometry
    clipboard.h               clipboard/native selection exchange
    window_services.h         cursor, native capture, focus notifications
    win32/                    native implementations
    linux/x11/                native X11 implementations
    linux/wayland/            native Wayland implementations
  frame/                      native window/event-loop owner; hosts OS services
impl/
  ui/                         nwb_ui: CPU toolkit, no ECS or GPU dependency
    context.{h,cpp}            identity, state, frame lifecycle, actions
    layout/                   measure/arrange, containers, scrolling
    input/                    hit testing, focus, capture, navigation
    text/                     fonts, shaping, text layout
    edit/                     Unicode model, commands, navigation, history, composition
    paint/                    primitives and immutable paint output
    widgets/                  controls composed from the above services
    style/                    skin region resolution, metrics, control states
    gpu/                      nwb_ui_gpu: uploads, atlases, raster, resources
  assets_ui_skin/              typed skin/atlas asset, schema, cook, runtime load
  ecs_ui/                     nwb_ecs_ui: world callbacks and runtime adapter
  assets/graphics/ui/          raster shaders and .nwb metadata
  assets/ui/skins/default/     one default skin texture and atlas .nwb
tests/
  unit/os/                    service/session contracts using test doubles
  unit/ui/                    CPU toolkit tests
  unit/ecs_graphics/          renderer/UI integration contracts
  smoke/                      interactive gallery and GPU captures
```

`nwb_ecs_ui` depends on `nwb_ui`, `nwb_ui_gpu`, the OS service interfaces, and the existing ECS/input/runtime modules. `nwb_ui_gpu` depends on the CPU paint contract and existing graphics, asset, and task modules. The renderer consumes a generic prepared presentation layer, never widget classes. `nwb_os` has no dependency on `impl/`, ECS, or graphics. Widget/layout policy stays in `impl/`.

`core/os/` provides clipboard/native-selection and text-input/IME services; broader window services can extend that module separately. `Frame` owns native windows and event pumping, constructs the OS service implementations using borrowed native window/display handles, and forwards required native messages. Keep those handles private to platform adapters. Required service references flow through `ProjectRuntimeContext` (`loader/project_entry.h`) and are borrowed by `UiLayerSystem` and `UiPaintContext`; avoid global lookups and a reverse dependency from `nwb_os` to `nwb_frame`. Native resources outlive their service objects, and service objects outlive borrowers.

Use an immediate/declarative builder over retained state. Application callbacks describe the current UI; stable IDs retain focus, selection, window placement, scroll offsets, and edit history. This preserves the convenient callback workflow while allowing independent layout, painting, and rendering. Avoid an entity or GPU task per widget.

Core contracts:

- `UiContext` owns a context-local state store using an explicit caller-owned arena. IDs include the UI root/entity generation, parent scope, and a stable application key. List item IDs follow model keys rather than row numbers. Diagnose duplicate IDs; retire removed state and release its focus/capture safely.
- `UiBuilder` declares controls and layout. `UiDrawContext` gains an explicit builder/context reference; application models remain application-owned. Define action timing: process queued input against the last committed layout, commit typed actions/model changes on the owning thread, then build/layout/paint the next frame. Do not imply that a newly laid-out control was hit-tested before it existed.
- `UiPaintList` contains ordered rectangles, rounded rectangles, borders, lines, image quads, glyph runs, and clip operations. Controls never record graphics commands. Initial clipping can use intersected rectangular scissors; complex path/stencil clipping is a later feature.
- `UiFrameSnapshot` owns immutable geometry, ordered draw commands, upload bytes, typed texture/font identities, copied metadata, clip bounds, display metrics, and a generation. Its GPU owner retains resolved texture/font resource versions through completion; typed asset identities alone provide no lifetime retention. GPU work reads this snapshot, never live widgets, ECS data, or user callbacks.
- `UiTextureHandle` and font handles are engine-owned, generation-checked handles. Use typed asset references for font/image asset bindings. Convert to backend resources and descriptor handles only in the GPU layer.
- Skin assets define atlas regions, state appearances, spacing, typography roles, borders, and colors. Widgets request semantic parts rather than hardcoded UVs. Store layout in logical units; convert to physical pixels consistently for painting, hit testing, scissors, and the IME caret rectangle.

Initially keep input, application callbacks, layout, and snapshot publication on the main thread. Later move pure shaping/tessellation of frozen data to CPU tasks if measurements justify it. Font cache mutation needs a clear owner even when jobs compute glyphs. Independent GPU scheduling does not require multithreaded widget callbacks.

## 3. GPU work and final composition

Target dependency graph:

```mermaid
flowchart LR
    A[UI input and model actions] --> B[Build, layout, paint snapshot]
    B --> C[UI geometry and glyph uploads]
    C --> D[UI raster into transparent layer]
    E[Scene rendering and effects] --> F[Linear scene composite]
    F --> G[Final scene display mapping, UI composition, output encoding]
    D --> G
    H[Acquired backbuffer availability] --> G
    G --> I[Final timing and presentation endpoint]
```

Use separate tasks in the existing frame graph. A new scheduler, independent submission loop, or forced extra queue would duplicate lifecycle ownership. UI raster requires Graphics capability; uploads use the existing built-ins. Let the compiler choose legal queues and packet placement. A separate task makes independent scheduling possible but does not promise simultaneous execution on a GPU with one graphics queue. Measure actual overlap with scene compute work before claiming a speedup.

### Integration contract

Introduce a small generic pre-output layer-contributor interface beside the existing presentation contributor. Preparation freezes CPU data and resolves resource readiness; declaration adds UI upload/raster tasks and returns a typed layer texture import, readiness task, extent, and an explicit linear/premultiplied color contract. Keep its types independent of `impl/ui/`.

Declare this branch without a dependency on scene presentation or swapchain acquisition. In `DeferredGraphSuffixBuilder`, give the final output task dependencies on both scene composite readiness and layer readiness, and declare the UI texture read. The current post-output contributor can remain temporarily for unmigrated ImGui UI. Preserve the final timing endpoint, presentation signal, and accepted-queue recovery behavior after all contributors. Update the explicit overlay/endpoint validation in `renderer_frame_pipeline_execute.cpp` to validate the new producer/consumer contract.

The existing frame timer begins inside `ShadowPrepareGraphTask::record()` in `impl/ecs_render/raytrace/task_graph_shadow_prepare_tasks.cpp`. An independent UI branch could run before it. Move whole-frame timing to a small shared prelude before both branches, with the end after their final join, or explicitly keep that metric scene-only and add correctly scoped frame/UI metrics. Preserve timing transaction recovery. Do not make UI depend on all shadow preparation just to inherit its timing start.

The final compositor can sample the UI layer in the existing fullscreen output pass. There is no need for an additional full-screen pass merely to merge UI. A standalone UI-only path must also work when no scene renderer contributes: use a defined background and the same layer/output contract, acquire/present once, and avoid submitting the UI twice when a scene renderer already claims it.

### Render and color contract

- Allocate a transparent, single-sample color target at the physical presentation extent. Clear to `(0, 0, 0, 0)`. Use a linear `RGBA16_FLOAT` target as the initial quality reference, subject to device format support; evaluate a linear `RGBA8_UNORM` option against precision and bandwidth measurements.
- Decode authored sRGB colors/images to linear values exactly once. Treat glyph atlas samples as coverage. Generate premultiplied output and blend both color and alpha with `ONE, ONE_MINUS_SRC_ALPHA`; preserve draw order and merge only compatible adjacent batches.
- Composite with `result.rgb = ui.rgb + scene.rgb * (1 - ui.a)`, where both inputs use the same linear display color space and luminance units. `ui.rgb` is already premultiplied.
- For SDR, apply the existing scene exposure/tone mapping first, then UI composition, then the attachment's sRGB conversion. Keep UI appearance independent of scene exposure.
- For HDR10, factor the current combined scene mapping/PQ helper into display-linear mapping and encoding. Convert mapped scene and UI into a common linear Rec.2020/nits representation, scale UI by the existing paper-white policy, composite, then encode PQ once. Do not apply the scene shoulder to UI or blend UI into already-PQ-encoded pixels.
- Use an absent-layer flag/variant for empty UI; skip UI raster and sampling without manufacturing empty presentation work. Pending font/texture uploads may still require upload/completion tasks.

An RGBA16F layer at 3840 x 2160 costs about 63.3 MiB before allocator overhead; three in-flight copies cost about 190 MiB. Budget this explicitly. Prefer adding one UI texture to the existing final pass over adding another scene-sized intermediate. Keep damage tracking, cached subtrees, partial target updates, and alternate formats as measured follow-up optimizations.

### Resource and failure invariants

- Bind one immutable snapshot and writable buffer/target set to each in-flight frame slot. Reuse them only after that slot's completion, not merely after a CPU frame count or successful submission.
- Declare all vertex/index/texture reads and render-target writes, including graph transitions and queue ownership. Bindless access does not remove the need for resource declarations. Reuse the existing alignment, retained upload, import-generation, and descriptor lifetime rules.
- Glyph atlas updates must not overwrite texels referenced by in-flight draws. Use safe append-only regions, versioned pages, or explicit synchronization. Eviction/repacking retires old pages and descriptors after their final use completes.
- Separate upload/raster submission acceptance from final-compositor submission acceptance and GPU completion. The current terminal UI task confirms presentation in its `accepted()` callback; that cannot remain the meaning of an offscreen raster callback. The layer contract needs final-consumer acceptance/completion notification as well as producer readiness. If raster is accepted and final output rejects, retain the layer/snapshot for a correctly synchronized retry or UI-only fallback without replaying input/actions. Retire after the last accepted consumer completes, or after producer completion when no consumer will use it. Keep the interaction-layout generation explicit when the displayed frame has not advanced.
- Create/resize targets, descriptors, and framebuffer-compatible pipelines in resource setup. Schedule dynamic atlas growth through preparation/setup; native record callbacks only consume prepared resources.
- Resize, DPI, output-format changes, minimize/restore, context destruction, rejected compilation, rejected submission, and device recreation need explicit invalidation/retirement behavior. A rejected graph must not mark uploads complete, reuse GPU-visible storage, or replay application actions.
- UI preparation failure before graph declaration can select an explicit scene-only frame with diagnostics. Failure after partial submission follows the existing frame recovery transaction. Do not silently present stale UI while hit-testing a newer layout.
- Image widgets showing current scene results must declare their real producer dependencies. Such widgets intentionally limit UI independence; use a prior completed preview only when the application accepts the latency and the image is versioned safely.

## 4. OS services, input, fonts, and editing foundations

### OS ownership and the borrowed UI bridge

| Owner | Responsibilities |
| --- | --- |
| OS feature layer (`core/os/`) | Native IME sessions and protocol handling; composition/commit events; candidate-window positioning; clipboard and native selection ownership/transfer; cursor shape; native capture and focus/capture-loss notifications. |
| Native frame (`core/frame/`) | Native window/display lifetime and event pump; creation/teardown of services; forwarding messages to the appropriate OS service and existing input dispatcher. |
| ECS UI adapter (`impl/ecs_ui/`) | Borrow service references, associate the focused control with a text-input session, map coordinates, route normalized events, and translate copy/paste/cursor/capture intents into service requests. |
| Toolkit (`impl/ui/`) | Widget focus, caret/selected text range, edit buffer, undo/redo, validation, preedit display, and paint geometry. It emits platform intents and consumes normalized results without native APIs. |

Here, native selection exchange means OS-managed transfer such as a platform's primary selection channel. Its ownership and protocol belong to the OS layer. The edit box's selected range remains local widget state; the adapter can publish the selected text through an available OS channel. Neither clipboard ownership nor an IME session owns the widget's edit model.

Use capability-aware contracts rather than assuming every backend provides identical features. `interface ITextInputService` and `interface IClipboardService` are implemented; a broader `IWindowServices` remains separate work. Services are scoped to the native window/seat where needed. Native IME sessions report composition/preedit/commit/cancel and UTF8 byte selection ranges, accept surrounding text when supported, and accept the current caret rectangle. The borrowed edit host suppresses duplicate committed delivery through both the native session and `keyboardCharInput`.

Clipboard reads and native selection transfers use request/completion contracts so event-loop-driven backends do not require waiting for an external clipboard owner on the UI thread. OS tokens carry service/request generations; the adapter separately retains its widget/edit generation and rejects stale completions after focus changes, widget destruction, or cancellation. A cut removes selected text only after the copy operation succeeds. Return explicit unsupported/unavailable/failure results. The initial Windows backend executes admitted operations synchronously during the event-thread pump without retrying a busy clipboard; later backends can complete requests through native events. Keep native candidate-window rendering in the OS/IME and render only preedit/caret/selection within the widget.

All native service calls and callbacks obey their platform event-thread requirements. `ecs_ui` releases subscriptions, cancels pending requests, and ends its borrowed text-input session before destruction; it never destroys the shared clipboard/IME service. Focus loss has an explicit composition commit/cancel policy and releases native capture. Unit-test these contracts with fake OS services independently of the UI, and qualify native implementations with platform smoke tests.

### Toolkit input and text

Centralize z-order hit testing, pointer capture, focus scopes, Tab/Shift-Tab navigation, keyboard activation, and popup/modal routing. A modal blocks lower layers; a popup escapes its parent's clipping but remains within its root viewport. Outside-click dismissal must specify whether it consumes that click. Focus loss or destroyed controls cancels active interactions and releases capture.

Preserve `wantsKeyboardCapture()`, `wantsMouseCapture()`, and `wantsTextInput()` at the ECS adapter boundary for existing camera routing. Derive them from committed UI focus/capture/modal state. During temporary coexistence, one router decides whether custom UI, ImGui, or the scene receives an event. Account for the actual reverse traversal in `InputDispatcher`; registration names alone do not establish priority.

Implement the OS contracts in Windows, X11, and Wayland adapters separately. Existing Unicode character callbacks do not establish IME support. Test Korean composition explicitly, along with combining characters and surrogate/non-BMP input; do not label untested platforms as supported. No native clipboard or IME implementation belongs in `impl/ecs_ui/` or a widget source file.

Build a font service with explicit fallback fonts, glyph metrics, shaping/layout caches, DPI-specific rasterization, and bounded atlas pages. Package a licensed deterministic default font through the asset pipeline; do not depend on the font bundled inside ImGui or whichever fonts happen to be installed.

Recommended dependency boundary: keep controls and rendering engine-owned while using a font rasterizer and shaping library behind narrow text interfaces. FreeType supplies glyph images and metrics, while HarfBuzz shapes text into positioned glyphs; neither is a complete UI/layout system. Pin any selected dependency through the existing vendor process. See the [FreeType overview](https://freetype.org/freetype2/docs/index.html) and [HarfBuzz manual](https://harfbuzz.github.io/what-does-harfbuzz-do.html).

Store application text as UTF-8, but make edit navigation/deletion selection-aware and based on grapheme boundaries rather than bytes. Keep mappings among byte offsets, grapheme boundaries, shaped clusters, and visual caret positions. Bidirectional layout and line breaking need their own policies in addition to shaping. The segmentation baseline is [Unicode UAX #29](https://www.unicode.org/reports/tr29/); pin the implemented Unicode data version and fixtures.

## 5. Customizable texture/atlas skins

Ship exactly one engine-default skin texture with one accompanying atlas-info `.nwb` asset. Users can provide a replacement texture and atlas `.nwb` through the same asset pipeline and choose that skin through a typed `AssetRef<UiSkin>`. Widgets and their interaction code remain the same. The existing texture pipeline may also require its normal texture metadata/payload files; those are texture packaging, not an additional skin configuration format.

`impl/assets_ui_skin/` provides an engine-owned `UiSkin` asset using the existing typed asset, metadata, binary payload, cook, and load conventions. The skin stores a typed `AssetRef<Texture>` and validated atlas/style data. Keep CPU asset decoding independent of graphics; the UI GPU layer resolves the texture through `impl/assets_texture/loader.h`, reusing its resource/descriptor lifetime machinery. The metadata type is registered with the existing MetaScript `.nwb` asset pipeline. Concrete version-1 field syntax is documented in `impl/assets_ui_skin/README.md`; the table below also includes semantic roles and policies for subsequent widget milestones.

Follow `impl/assets_sampler/` for the small asset module: schema/asset header, versioned binary payload, runtime codec, cook implementation, volume registration, and separate runtime/cook CMake targets. Register in `impl/CMakeLists.txt`; retain the cook registrar through `pipeline/asset_builder/CMakeLists.txt` and the runtime codec registrar in consuming application links such as `CoolStuff/Testbed/CMakeLists.txt`. Extend the existing asset integration tests with skin cook/load coverage.

Declare packaging dependencies explicitly. `pipeline/dependeny_computer/module.cpp` currently passes input paths through, so `AssetRef<Texture>` alone does not guarantee discovery or inclusion. Ensure the chosen input roots contain both the atlas and referenced texture metadata/payload, and implement dependency discovery for targeted skin builds. Changes to either texture content or atlas metadata must invalidate the right cooked output. Do not imply that the current texture cooker consumes arbitrary PNG files directly; authoring artwork goes through the established texture conversion workflow.

| Atlas information | Purpose and rules |
| --- | --- |
| Schema version and texture reference | Identify the contract and the existing texture asset; track texture changes as asset dependencies. |
| Atlas extent and reference pixel density | Validate coordinates against the actual texture and convert artwork dimensions into logical layout units. |
| Named regions | Top-left origin pixel rectangles `(x, y, width, height)` with a fixed UV convention; names identify appearances such as `button.normal`, `edit.focused`, `checkbox.checked`, and `combo.arrow`. UVs are derived by the asset/runtime layer. |
| Draw mode and slice insets | Support a simple sprite and nine-slice stretch for resizable panels/buttons/edit boxes. Validate left+right and top+bottom against region bounds. Preserve corner sizes; define the minimum control size and behavior below it. Tiling is a later optional mode. |
| Content metrics | Logical padding, minimum sizes, border thickness, and optional alignment/pivot information. Keep content padding separate from the nine-slice border. |
| Semantic part/state mappings | Map widget parts and normal/hover/pressed/disabled/selected states to region IDs. Focus is an independent overlay so a control can be both selected and focused. Define state precedence and allow explicitly declared fallback to the normal state within the selected skin. |
| Text and tint roles | Named text/selection/caret colors, optional sprite tints, and typography roles. Define each color space and apply tint once in the linear paint path. |
| Sampling policy | Filtering, alpha convention, and atlas padding requirements. Validate against the texture asset's color-space/alpha contract and sampler choice. |

A combo box composes skin parts for its field/button, arrow, popup background, list rows, and selection; it does not bake those into a single fixed-size image. Window borders, scrollbars, buttons, and edit boxes similarly share sprite/nine-slice painting. Basic geometric primitives remain available for content, selection rectangles, carets, and custom drawing, while standard control appearances resolve through the skin.

Use transparent gutters/extruded edge texels as appropriate for the authored alpha convention to avoid neighboring-region bleed. Initially use the base mip with clamp sampling and sufficient per-region gutters; texture-wide clamp alone does not isolate atlas regions. If mipmaps or tiling are added, require atlas-aware padding/mip generation and image tests. Decode sRGB texture RGB once, retain linear alpha coverage, and premultiply the resulting shaded color once for the offscreen layer. Compare the existing texture compression path against thin borders and small icons before choosing a shipping texture format.

The default skin must cover every shipped widget and state with a documented region contract. Required missing regions, duplicate identifiers, invalid rectangles/insets, non-2D or incompatible texture dimensions, and unsupported schema fields fail cooking with actionable errors. An omitted skin selects the engine default. A failed custom skin load reports the failure and keeps a known-good skin (the default at startup); do not render missing controls or silently mix default artwork into a supposedly complete replacement. Any supported state fallback must be explicit in the schema.

Bind one immutable skin version to each UI frame snapshot, including layout metrics. If the application switches or reloads a skin, validate/load the new atlas and texture together, then activate them at a frame boundary, invalidate affected layout/paint caches, and retain the old GPU resources until completion. The atlas, metrics, hit-test layout, and sampled texture must never come from different versions.

The skin atlas contains control artwork and icons. Dynamic font glyph atlases remain a separate text-service resource so arbitrary input and fallback fonts can work; application image widgets can also use independent textures. The requirement for one default skin texture does not imply all text and application images must fit in that same immutable texture.

## 6. Component dependency order

| Stage | Components/services | Required behavior |
| --- | --- | --- |
| Foundation | Context, IDs, skin/atlas resolution, row/column/overlay layout, padding/alignment, clipping | Stable state, nested clipping, logical-to-physical scaling, deterministic paint order, sprite/nine-slice drawing. |
| Current parity | Label, separator, panel/window | Title bar, dragging, collapse/resize behavior as applicable, first-use placement, size rules, capture, and the current Testbed text. Decide explicitly whether to retain cross-run window placement. |
| Basic controls | Button, checkbox, radio, slider, progress, image | Keyboard activation, focus/disabled states, constrained values, typed change/activation events, declared image resource dependencies. |
| Scrolling and selection | Scroll geometry/state, scrollbar, selectable row, fixed-height virtualized list | Implemented: positioned wheel input, keyboard navigation, stable keyed selection, ensure-visible, clipped visible-row shaping and scrollbar dragging. Generic variable-height scroll containers remain separate work. |
| Popup infrastructure | Popup and modal scopes, tooltips, context menus and nested popup composition | Implemented: overlay ordering, anchoring/viewport clamping, focus trapping/restoration, lifetime-scoped actions, Escape/outside-click policy, delayed passive tooltips, stable-key menu commands and independent nested popup layouts. |
| Text entry | Single-line edit box, numeric edit, then multiline edit | Caret, selection, word/grapheme navigation, clipboard, undo/redo, horizontal/vertical scrolling, commit/cancel, validation, composition rendering, and IME integration. |
| Compound controls | Combo box, searchable combo, list box | Compose a button/edit box, popup, and list. Support keyboard open/select/cancel, selected-item visibility, and large data sets without building every row. |
| Later tools | Tree, table, tabs, menus, splitters, color picker | Add from demonstrated application demand; keep shared behavior in the existing services. Docking/detached windows are a separate project. |

A text edit model should be testable without graphics. Distinguish temporary editing text from a committed typed value: numeric edits must allow intermediate strings such as `-` without corrupting the application model. IME preedit is transient and must not enter undo history or application data until committed. Programmatic model changes need an explicit policy for active edits and selection preservation.

## 7. Delivery milestones and exit gates

| Milestone | Concrete delivery | Exit gate |
| --- | --- | --- |
| M0: Baseline and contracts | Record Testbed behavior/captures, input ownership, existing graph/timing behavior, platform/font scope, and target-device CPU/GPU/memory baselines. Agree on OS service ownership/borrowing, skin atlas schema and semantic parts, snapshot, action timing, layer/color, and failure contracts. | Current behavior and measurable comparison workloads are written down; no unbounded claim of widget parity. |
| M1: Toolkit foundations | Add `nwb_ui`, stable state, builder/layout/paint contracts, main-thread input routing, and deterministic font/text foundation. Add `UiSkin` asset cook/load, sprite/nine-slice paint, and the default texture/atlas. Define `nwb_os` interfaces and borrowed context wiring with test doubles; implement initial window services. | Layout, IDs, focus, capture, text measurement, immutable snapshot ownership, skin metadata validation, and OS/UI dependency direction have deterministic coverage without ImGui. |
| M2: Independent GPU layer | Add `nwb_ui_gpu`, owned geometry/atlas resources, upload/raster tasks, generic pre-output contribution, final SDR/HDR composition, standalone UI-only output, and timing markers. Can run in parallel with M1 once the paint contract is fixed. | A rectangle/image/text sample reaches the framebuffer through an independent UI branch; scene output is the final join; lifetime, resize, rejection, and color tests pass. |
| M3: Port and retire ImGui | Implement current window/label/separator parity using the default skin, migrate Testbed to an explicit custom context, qualify input/camera routing, then remove ImGui runtime/build/assets/vendor dependency. Demonstrate a replacement texture/atlas with the same widgets. | Clean build and asset cook without ImGui; current application behavior and custom skin selection work; UI-only/scene paths and supported SDR/HDR routes pass. ImGui retirement is complete here. |
| M4: Reusable controls | Add basic controls, scrolling/virtualized lists, popup/modal behavior, and the single-line edit model. Implement clipboard/native selection and IME backends in `core/os/` in parallel, connected through the borrowed adapter. | Gallery demonstrates mouse and keyboard behavior; large lists scale with visible rows; edit selection/undo/clipboard are covered. OS services are independently testable. |
| M5: Complete requested controls | Finish OS-backed IME integration, numeric/multiline editing, combo/searchable combo/list box, default-skin coverage for every shipped control/state, and target-platform qualification. | Edit box, combo box, and list meet the behavioral matrix with default and replacement skins; composition/clipboard/Unicode and native service lifecycle tests pass on claimed platforms. |
| M6: Measured optimization | Compare baseline and custom UI on representative hardware; tune batching, allocation reuse, glyph caching, target format, and optional damaged-region caching. | Measured CPU, GPU, memory, and input-to-presentation results meet budgets established in M0, with no visual or lifecycle regression. |

Critical path: M0 -> M1/M2 -> M3. The larger controls path continues through M4/M5. The OS service workstream can proceed independently alongside GPU/toolkit work after its contracts stabilize; it has no widget dependency. M3 needs the native window services used by current parity, but does not wait for full IME, docking, a table control, or full editor functionality.

Selectable keyed rows and fixed-height list virtualization now use the accepted popup/input contracts. `interface IListDataSource` supplies stable keys, efficient reverse lookup and enabled-row search; only clipped visible rows are shaped and painted. Model/source lifetimes fence queued row, navigation, wheel and thumb intentions. Combo selection now composes an automatic trigger/popup with this list, including selected-item visibility and explicit keyboard commit/cancel behavior. Searchable variants and editor/tooltip/context-menu work remain later increments. M6 optimization remains measurement-driven after these behavior contracts are covered.

## 8. Validation and performance

The first increment adds behavioral tests for its paint, clipboard, and skin contracts. Each subsequent milestone should extend coverage as its subsystem becomes real:

- CPU unit tests: ID/state retirement, layout constraints and clipping, scaled hit tests, focus traversal, capture cancellation, modal routing, selection across list reorder/removal, visible-row limits, and text edit transactions. Use a controllable clock for repeat, caret blink, and double-click behavior.
- OS service contracts: borrowed lifetimes, window/session identity, capability reporting, asynchronous clipboard cancellation, stale completions, composition cancellation, native selection ownership loss, duplicate commit prevention, and event-thread delivery. Verify `nwb_os` builds without UI/ECS and toolkit edit tests run with no native window.
- Skin asset tests: `.nwb` cook/load round trip, typed texture dependency invalidation, region bounds, duplicate/missing parts, state fallback rules, nine-slice insets, DPI metrics, malformed schema rejection, and atomic skin version changes. Gallery captures compare default and replacement skins, very small/large controls, transparent edges, thin borders, and atlas region bleed.
- GPU/task integration: uploads precede reads, the UI branch has no accidental scene-output dependency, the final output waits on both branches and acquisition, correct target transitions, retained descriptors/snapshots, rejected submission, safe retry, and no double presentation. Explicitly test accepted UI raster followed by rejected final composition, and timing that includes a UI branch scheduled before scene shadow preparation. Extend `tests/unit/task/gpu/task_graph_presentation_tests.cpp` and `tests/unit/ecs_graphics/task_graph_timing_contract_tests.cpp`.
- Image captures: transparent/opaque overlaps, glyph edges on light/dark backgrounds, clipped controls, popups, texture color spaces, multiple DPI values, odd framebuffer dimensions, and SDR/HDR paper-white behavior. Use tolerances and bundled fonts; validate numeric color transformations independently of monitor screenshots.
- Runtime smoke: scene plus UI, UI-only frame, empty UI with and without pending uploads, frame-slot pressure, atlas growth, destruction during interaction, rapid resize, minimize/restore, focus loss, and supported recreation paths. Exercise supported Windows/Linux backends rather than inferring cross-platform success.
- Input scenarios: keyboard-only navigation, mouse drag outside bounds, first-click capture, no camera movement while editing, clipboard round trips, Korean preedit/commit/cancel, combining marks, emoji/non-BMP input, undo/redo, and external model updates during editing.
- Performance workloads: current Testbed panel, many visible controls, a 100,000-item virtualized list, long editable text, repeated glyph misses, and high-resolution UI. Capture CPU build/layout/paint time, upload bytes, allocations, batch counts, atlas/target memory, `ui.raster` GPU time, final-output cost, and accepted input-to-presentation latency. Report cold and warm cases separately.

Offscreen composition adds memory traffic and is not automatically faster than the current tiny direct overlay. The architectural benefit is independent preparation and a reusable UI layer; performance claims require whole-frame measurements on the target GPU. Share the existing task telemetry and capture infrastructure rather than creating a parallel profiler.

## 9. ImGui removal checklist

All runtime/build/asset/vendor removal items below are implemented in the M3 increment. The fresh build/cook and dependency audit are its verification gates.

1. Replace the `ImGui` calls in `CoolStuff/Testbed/ui_controls.cpp` and update callback wiring to pass the custom context explicitly.
2. Remove ImGui context/input/frame/draw/texture types from `impl/ecs_ui/`, including `system.h`, `system.cpp`, `system_frame.cpp`, `input_events.cpp`, graphics/texture helpers, task payloads, and legacy presentation. Move reusable GPU mechanics into `impl/ui/gpu/`; retire callback-specific machinery when no caller needs it.
3. Replace `impl/assets/graphics/imgui/` shaders, binding headers, names, and `.nwb` metadata with engine-owned UI assets; update asset references and recook the actual runtime volumes.
4. Remove `nwb::imgui` from `impl/ecs_ui/CMakeLists.txt`, `add_subdirectory(imgui)` and `nwb_vendor_imgui` runtime-list registration from `3rd_parties/CMakeLists.txt`, then remove `3rd_parties/imgui/`. Any separately retained font/edit utility must be an explicit independently owned dependency, not an include into the deleted vendor tree.
5. Replace obsolete ImGui examples/comments in generic presentation contracts; update affected source-policy and architecture tests to the final custom-UI contract. Preserve generic submission/recovery/timing tests.
6. Search tracked production/build/asset sources for `ImGui`, `ImDraw`, `ImTexture`, `imgui.h`, ImGui shader paths, and vendor targets. Exclude historical documentation and generated artifacts from the zero-runtime-dependency gate; verify a fresh build/cook cannot resolve the removed directory accidentally.

Completion has two explicit gates: M3 removes the dependency completely; M5 delivers the requested reusable edit box, combo box, and list toolkit. The engine owns UI behavior, state, paint data, and GPU integration throughout the final design.

## Builder scope storage foundation

`BuilderScopeFrame` now owns each declaration scope's transient layout, items, deferred control loans, container stack and window/popup state in the caller-owned UI arena. Shared text, paint, skin, styles and edit services remain on `Builder`. The embedded frame is immovable and accessed through a non-null reseatable pointer so subsequent popup composition can preserve parent scopes without copying borrowed state. Public behavior and the current popup nesting restrictions are unchanged in this infrastructure increment.

Validation on Windows ARM64 / Clang, `dbg`: all 619 UI and 71 ECS-UI tests pass. The window, edit, popup, combo, searchable-combo and popup-tools GPU smoke tests all pass. Genuine Linux syntax/type checks pass for 165 translation units with unchanged input and dependency hashes; native Linux linking and compositor behavior remain unqualified. All 22 changed C++ sources pass the source-format and size checks, and `git diff --check` passes.

## Nested popup composition increment

User popup scopes now compose inside other user popups, including open row/column containers, with independent layout and exact parent restoration. Stable arena-owned child frames retain their model/source loans through the outermost end. Buffered Builder subtree emission waits until that end, so closed or reopened ancestors suppress all deferred descendant source work. Source/model mutations during painting reject the candidate while preserving the application mutation; callback-free ancestry checks stop later stale subtree work. Reentrant Builder reset, declaration and configuration changes during control preparation or final emission also reject the candidate before clearing borrowed storage.

Context separates declaration-order layer reservation from temporary activation and geometry finalization. Eight registered popup scopes is the frame limit, including combo/search/context-menu children. Router snapshots validate exact earlier/lower parent tokens; child dismissal, parent focus restoration, ancestor fencing and unrelated top-level chains retain copied held-input ownership. Plain, combo, search and menu openings bind to their exact parent opening and retire stale descendants on rebind, while deliberate fresh openings can bind to the replacement parent. All paint still joins the same independent UI GPU texture and final framebuffer compositor.

The implementation and regression sources are split among scope storage, popup-family emission, control state/input/paint, Context, router, ECS edit-session fixtures and separate native/gallery domains. The remaining sequential steps are multiline editing, radio/slider/progress/image widgets, OS qualification and Wayland selection-exclusion handling, then asset/render integration. M6 measurement and tuning remain deferred.

Nested qualification on Windows ARM64 / Clang, `dbg`: all 688 toolkit and 75 ECS UI tests pass, including three-level automatic popup triggers, four source-callback Builder reentry attacks, queued native/clipboard ownership and different-anchor secondary-menu focus restoration. Both nested skins pass 48 native/displayed/pixel gates. Fifteen unaffected existing UI/Testbed GPU fixtures pass; the two popup-tools regressions are separately qualified after isolating held-key checks from anchor hover and maintaining the requested Win32 cursor during captures. Frozen nested captures were visually inspected for parent restoration, escaped child clips and alternate query appearance.

Genuine Linux x86_64 syntax/type checking passes for 178 distinct translation units with X11 and Wayland text-input-v3/primary selection definitions. All 9,918 hashed inputs and 1,193 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking, compositor execution, physical pointer grabs and live IME remain unqualified on this Windows host. All 61 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and 800-line size checks; the largest is 698 lines. Changed Python drivers pass AST/encoding/line-ending checks, and `git diff --check` passes.

## Ordered component edit actions foundation

`IEditActionSink` now receives Submit, Cancel, Blur and Abandon while the edit model is synchronously lent, between copied input events. `IEditBoxHost::editActions()` is an optional capability with an invalid default result, so a composite editor cannot silently fall back to unordered aggregate results. A Submit callback that canonicalizes text still permits later copied commits in their original position; surrounding deletion requires the exact live published session.

The ECS host distinguishes ordinary focus transfer from native focus loss, removal, reset, rebinding and enabled/read-only policy changes. Ordinary Blur keeps preceding input and runs at the matching focus epoch. The other lifetimes Abandon on the next live declaration, with no retained model or sink pointer. Cancel restores through the component sink, fences its remaining epoch and preserves an independently acquired later focus. Same-owner external draft replacement remains authoritative while queued old input is discarded. Public host mutation during a loan rejects before storage can be cleared.

Registry/frame/geometry, copied input collection, model/session loans and action application are separate sources. Shared fake OS services support focused event-order and lifetime regressions. This foundation precedes the numeric model and Builder widget increment.

Qualification on Windows ARM64 / Clang, `dbg`: all 688 toolkit and 98 ECS UI tests pass, including 23 ordered-action traces. The ordinary edit, searchable-combo and nested-popup GPU regressions pass. Genuine Linux x86_64 syntax/type checking passes for 181 translation units; all 9924 hashed inputs and 1,199 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking, compositor execution and live IME remain unqualified. All 12 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 384 lines); authored text and `git diff --check` also pass.

## Numeric editor increment

`IntegerEditModel` and `FloatEditModel` own exact committed values separately from bounded editable drafts. Decimal parsing classifies complete, incomplete, invalid and out-of-range input before conversion; integer values never pass through floating point, and float formatting preserves signed zero and finite bit roundtrips. Invalid intermediate input stays editable. Submit accepts valid lexical text without disturbing selection/history, Blur canonicalizes or restores, and Cancel/Abandon restore the committed value. Reject/Clamp bounds remain separate from representation overflow; external setters fence prior input even when the replacement is unchanged.

`Builder::integerEdit()` and `floatEdit()` share the normal edit-box host, skin, layout and OS services through ordered action sinks. Per-scope typed loans validate numeric/draft revisions, selections and composition through deferred popup-family work and release at the enclosing end. Reentrant callbacks, external mutation and aliased state reject the candidate before later borrowed work. Models, parsing/formatting, Builder action policy, loan validation, gallery, GPU scene/snapshot and native test/probe code remain separate sources in their owning domains.

Qualification on Windows ARM64 / Clang, `dbg`: all 772 toolkit and 98 ECS UI tests pass, including 49 numeric model/parser/formatter cases and 35 Builder action/lifetime cases. The ordinary edit, searchable-combo and nested-popup GPU regressions pass. Both numeric GPU fixtures pass all 77 native/displayed/pixel gates with default and replacement skins. Frozen captures were visually inspected. The fixtures exercise exact integers above 2^53, malformed/incomplete drafts, real OS clipboard copy/cut/paste, undo/redo, bounds, policy/focus changes, external replacement, ordered Submit input, resize and long-draft scrolling with default and replacement skins.

Genuine Linux x86_64 syntax/type checking passes for 196 translation units; all 9949 hashed inputs and 1224 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking, compositor execution and live IME remain unqualified on this Windows host. All 35 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 364 lines); authored Python passes AST/encoding/line-ending checks and `git diff --check` passes. M6 performance measurement and tuning remain deferred.

## Multiline content and selection lifetime foundation

`EditModel` now has an immutable SingleLine/Multiline mode, retaining SingleLine defaults. Multiline document bytes use canonical LF with strict UTF8/control admission, empty/trailing hard lines, grapheme selection/deletion and existing copied history/composition. External insertion preparation atomically maps platform line endings to LF and tabs to spaces. Borrowed clipboard and native Commit adapters apply that policy as one replacement; native preedit bytes and offsets stay unchanged. Enter/Shift+Enter insert LF, Ctrl+Enter submits, and hard-line/document Home/End have separate intents.

A monotonic selection generation advances on accepted selection intents, including identical positions, and on accepted text/history operations that replace selection. Expected native/clipboard state includes selection and composition generations; published surrounding eligibility also checks the expected local snapshot. Numeric and searchable query loans include the selection generation through preparation, callbacks and final popup-family validation. Selection/composition changes that return to their original values cannot revive queued work or an earlier borrowed snapshot. Rejected operations preserve generations.

Models/content policy, commands, native insertion, clipboard exchange, registry snapshots and deferred loan validation remain in their existing separate domains. The next multiline increment supplies per-line caret geometry, ordered vertical navigation and the visual text-area widget; this foundation does not claim that visual control is complete. M6 measurement and tuning remain deferred.

Qualification on Windows ARM64 / Clang, `dbg`: all 826 toolkit and 120 ECS UI tests pass. The new cases exercise multiline admission/normalization, hard-line commands, preedit byte offsets, one-operation clipboard/native history, and identical or away-and-back selection/composition lifetimes through queued services and deferred numeric/search callbacks. The ordinary edit, searchable-combo and numeric GPU regressions pass.

Genuine Linux x86_64 syntax/type checking passes for 206 translation units; all 9961 hashed inputs and 1236 actual dependencies remain unchanged during qualification, with no missing dependency hashes. The single corrected popup-focus assertion source was rechecked against the frozen inputs after the full batch. Native Linux linking, compositor execution and live IME remain unqualified on this Windows host. All 37 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 310 lines); authored text and `git diff --check` also pass.

## Shared multiline caret geometry increment

`EditCaretGeometry` now owns the adopted layout/font versions, display boundaries, mapped committed caret stops and per-hard-line records. Adoption validates matching source bytes, immutable edit mode, finite metrics, contiguous line/cluster coverage and scalar-safe LTR edges atomically. Caret interpolation still respects ligatures and native scalar positions within preedit graphemes. Pointer hits choose a hard line before an x stop; empty/trailing lines remain addressable and preedit-only lines map to committed replacement endpoints. Preferred-column adjacent-line targets and per-line ranges are explicit queries.

`EditBoxView` separates snapshot preparation, geometry ownership, placement and paint sources. Multiline arrangement is top-aligned and scrolls in both axes to reveal the caret. Selection and preedit paint use one segment per hard line, including a visible cap for selected LF bytes. The existing SingleLine API, vertical centering and horizontal-scroll behavior are preserved. Ordinary Builder edit boxes explicitly require SingleLine mode until the dedicated text-area declaration and ordered navigation host are implemented in the next increment.

Qualification on Windows ARM64 / Clang, `dbg`: all 877 toolkit and 120 ECS UI tests pass, including 36 direct geometry and 15 multiline View cases. The ordinary edit, searchable-combo and numeric GPU regressions pass.

Genuine Linux x86_64 syntax/type checking passes for 211 translation units; all 9969 hashed inputs and 1244 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking, compositor execution and live IME remain unqualified on this Windows host. All 12 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 435 lines); authored text and `git diff --check` also pass. M6 measurement and tuning remain deferred.

## Ordered vertical navigation host foundation

`EditNavigationState` owns a preferred column with immutable instance identity and monotonic intent generation. Accepted identical setters and resets retire old snapshots; invalid nonfinite setters preserve them. The synchronously borrowed `interface IEditNavigationResolver` receives the current Multiline model and returns target/column values. `IEditBoxHost::editNavigated()` requires this resolver, state and an ordered component action sink; unsupported hosts fail the declaration explicitly.

The ECS host resolves Up/Down/Page keys between copied events against current text, using viewport height copied from accepted geometry at key admission. Complete model/selection/composition and navigation snapshots fence callback mutation and host reentry before a grapheme target is committed. Active preedit invokes no resolver, read-only fields can navigate, and disabled fields cannot. Preferred-column resets follow accepted intent and completion epochs; Copy, unchanged Submit and uncompleted clipboard transfers preserve it. Blur/Cancel/Abandon and focus regain reset at their ordered positions, preserving keys that precede ordinary focus transfer. Rebinding and retired owners reset only during a subsequent live loan.

Published editor geometry now owns hard-line records, caret stops and mode. Pointer hits choose Y before X, including empty/trailing lines and preedit-only replacement endpoints, and continue using old accepted geometry while a candidate awaits publication. Navigation state, translation, registry/session loans, ordered actions, callback resolution and copied hit geometry remain separate sources in their owning domains. The dedicated Builder text-area control is the next increment; M6 measurement and tuning remain deferred.

Qualification on Windows ARM64 / Clang, `dbg`: all 887 toolkit and 160 ECS UI tests pass, including ten navigation-state/translation and forty ordered host/lifetime/hit cases. The ordinary edit, searchable-combo and numeric GPU regressions pass.

Genuine Linux x86_64 syntax/type checking passes for 220 translation units; all 9980 hashed inputs and 1255 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking, compositor execution and live IME remain unqualified on this Windows host. All 20 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 390 lines); authored text and `git diff --check` also pass. M6 measurement and tuning remain deferred.


## Multiline text-area widget increment

`Builder::textArea()` now lends a Multiline model and separate immovable viewport state through the enclosing scope or outermost popup end. The state owns two-axis scroll and preferred navigation; public scroll/reset epochs and complete model/navigation snapshots fence later callbacks and aliases. Declaration, state, loan validation, paint and fresh-shaping resolver remain separate sources. Existing edit skin parts serve the field and the default viewport height is 160 logical pixels.

Each vertical key resolves the current model between copied events. Page uses accepted viewport height, clamps the caret's line center in f64 and preserves the active caret at document boundaries. Explicit scroll remains authoritative across first binding, idle frames and resize; later observed editing/selection/composition/focus and known-model rebinding reveal the caret. Painting clamps both axes, emits per-hard-line selection/preedit ranges and publishes owned geometry through the existing ECS/OS bridge. Actions report submission/cancel/focus without rewriting the document.

The Testbed gallery and dedicated default/replacement-skin GPU fixtures exercise the public widget. Scrollbar/wheel control integration follows this increment. M6 performance measurement and tuning remain deferred.

Qualification on Windows ARM64 / Clang, `dbg`: all 959 toolkit and 160 ECS UI tests pass. The ordinary edit, searchable-combo and numeric GPU regressions pass, and both text-area skins pass all 42 displayed/native/pixel gates, including completed cross-line Cut and one Undo, canonical clipboard history, preferred-column navigation, resize and focus retirement.

Genuine Linux x86_64 syntax/type checking passes for 235 translation units; all 10,001 hashed inputs and 1,276 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking, compositor execution and live IME remain unqualified on this Windows host. All 33 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 521 lines); authored text and `git diff --check` also pass. M6 measurement and tuning remain deferred.


## Text-area wheel and two-axis scrollbar increment

Reusable scrollbar layout and skin style have separate owners. Coupled horizontal/vertical visibility reserves the exact content viewport, and View arrangement shares its existing caret/selection geometry with that reserved viewport. Wheel actions copy both axes once, thumb drags retain accepted press geometry, and track release pages by the accepted viewport. Read-only fields can scroll; disabled fields expose no active parts.

Text areas own copied scroll admission separately from model and preferred-column state. Complete model, public state, policy and viewport epochs retire old wheel/part input before application, while offset-only repaint preserves the drag. Auxiliary scroll-token changes preserve the editor focus and main text capture; scrollbar parts retain strict token lifetimes. A scrollbar press focuses the host without admitting a text selection. Edits drain before scroll application; changed model state conservatively retires older copied scroll input. No cross-queue chronology is claimed. M6 measurement and tuning remain deferred.

Qualification on Windows ARM64 / Clang, `dbg`: all 1,028 toolkit and 164 ECS UI tests pass. Both text-area skins pass all 65 displayed/native/pixel gates, with 57 markers per gate. The ordinary edit, default/replacement list, searchable-combo and numeric GPU regressions pass with graphics validation. The two corrected unit fixtures retain their original behavior checks while accounting for reserved scrollbar height and fresh-parent popup retirement. Native focus clicks stay inside the text viewport and park away from bars for strict normal-atlas keyboard probes.

Genuine Linux x86_64 syntax/type checking passes for 245 translation units; all 10,014 hashed inputs and 1,289 actual dependencies remain unchanged, with no missing dependency hashes. Native Linux linking/compositor execution and live IME remain unqualified on this Windows host. All 29 changed C++ sources pass UTF-8 without BOM, CRLF, banner/separator, exact EOF and size checks (maximum 559 lines); authored text and `git diff --check` also pass. M6 measurement and tuning remain deferred.
