// limztudio@gmail.com — engine source-architecture redesign ("neaten up")
================================================================================

# NWBLot — Source Architecture Redesign

Grounded in a full-codebase dependency survey (22-agent map/analyze/design/judge pass,
2026-07-05). This is a source-ORGANIZATION redesign — where code lives, module boundaries,
visibility, and god-file splitting — NOT a rewrite of algorithms, the ECS, the RHI, or the
asset pipeline. Every hard invariant is preserved (see §6).

--------------------------------------------------------------------------------
## 1. Diagnosis — what is actually wrong (and what is NOT)

The vertical layering is already **strictly acyclic and grep-clean**: `global < core < impl <
app`, zero upward includes. So this is **not a relayering**. The disease is three specific things:

1. **Mega-headers recompile the world.** `api.h` (3358) is parsed by every render TU; `backend.h`
   (2411) forces ~28 backend TUs to recompile on any private-member edit; `vector.h` (3484) and
   `global.h` (force-include umbrella) pull everything into everything. `core/graphics/module.h`
   holds `Backend m_backend` **by value**, so `volk`/Vulkan compiles into every render/ECS-render TU.

2. **Horizontal slicing gives a feature no home, so it piles into god-files.** A single feature (GI)
   is smeared across `impl/ecs_render/raytracing_system.cpp` + `impl/assets/graphics/gi/*` +
   `impl/assets_graphics/*`. Nothing structurally stops accretion → `raytracing_system.cpp` is
   **7354 lines** bundling RT-shadow + soft-shadow + caustics + GI/surfels over a shared SW-BVH.
   Meanwhile the **Slang shaders are already sliced per-feature** (`gi/`, `shadow/`, `caustic/`) —
   only the C++ lagged.

3. **Nothing physically prevents coupling.** `renderer_state.h` is an all-friends god-struct;
   `vulkan/backend.h` internals are reachable from anywhere; `core/common/` is a 7-concern junk
   drawer (`log.h` with 104 consumers + `Initializer` + `WinFrame` + `<windows.h>` mid-header).

Worst offenders (lines): raytracing_system.cpp 7354 · vector.h 3484 · api.h 3358 · material
cook.cpp 3071 · vulkan texture.cpp 2445 / raytracing.cpp 2419 / backend.h 2411 / backend_context.cpp
2228 / resource_bindings.cpp 2139 · filesystem/module.cpp 2137 · material bind.cpp 1779 · shader
cook.cpp 1525 · metascript parser.cpp 1280 · csg_interval_peel.cpp 1150 · renderer_state.h 771.

--------------------------------------------------------------------------------
## 2. Target end-state — "Keystone"

Two mechanisms, applied where the evidence points:

- **Physical public/private split** (the single strongest anti-piling law): a compiled module declares
  `target_include_directories(nwb_X PUBLIC include/ PRIVATE src/)`. Public API in `include/nwb/<module>/`;
  every `.cpp` and private header (`state.h`, `*_private.h`, `vulkan/*.h`) in `src/`. **If you cannot
  `#include` it, you cannot couple to it** — the friendship cliques and umbrella blast-radius simply
  stop compiling across targets.
- **Vertical feature slices — for the render tier ONLY**: each render feature becomes one directory /
  one target owning its own GPU state, its own targets, its cook code, and (by reference) its shaders,
  behind a narrow public interface. This matches the already-sliced shader tree and removes the shared
  file that behavior piles into.

Everything else (foundation, RHI, core services) keeps **strict interface hygiene**, not feature-slicing.

### Layer table (downward-only; each may depend only on layers below)

| L | Layer      | Holds                                                                                          |
|---|------------|------------------------------------------------------------------------------------------------|
| 0 | foundation | header-only DAG root (was `global/`): types, math (+extracted `simd/backend.h`), containers, strings/text, binary, name, **pure** path math, sync/smartptr/diagnostics. No `<windows.h>`. |
| 1 | runtime    | `nwb_os` (the `<windows.h>`/`<dirent.h>` firewall), `nwb_alloc` (pure memory), `nwb_thread` (NEW — carved out of `alloc`). |
| 2 | core       | `nwb_log` (facade/detail split), `nwb_bootstrap`, `nwb_namesym`, `nwb_ecs` (World *composes* `ThreadPool&`), `nwb_messaging`, perf/telemetry/crash, `nwb_volume`, `nwb_assets` (runtime-only), `nwb_assets_cook` (NWB_COOK), `nwb_metascript` (cook-tier), `nwb_mesh`. |
| 3 | RHI        | `nwb_rhi` (INTERFACE, backend-free vocab from exploded `api.h`), `nwb_rhi_services`, `nwb_graphics_vulkan` (backend, `backend.h` shattered), `nwb_graphics` (THIN facade, backend behind a pimpl). |
| 4 | host       | `nwb_frame` (Frame + relocated `WinFrame`/`LinuxFrame`, typed per-platform state), `nwb_input`. |
| 5 | impl       | asset codecs/cooks; `nwb_spatial` (Transform lifted out of scene); **`render/` vertical slices**: `kernel` + `swbvh` + `shadow` + `softshadow` + `caustics` + `gi` + `csg` + `avboit` + `skinning` + `ui`. `contract/graphics/**` = dual-consumed Slang/C++ ABI. |
| 6 | apps       | loader, testbed, resource_cooker, logger, tests, codec aggregators, build DSL.                  |

### Render tier (the heart of the change): `impl/ecs_render/` → `impl/render/<feature>/`

```
impl/render/
  kernel/       nwb_render_core   : RendererSystem spine + deferred + material-runtime + pass sequencing;
                                    renderer_state.h(771) split into per-feature state; GBufferTargets only
  swbvh/        nwb_render_swbvh  : SHARED SW-BVH substrate + SwBvhTraceResources (do NOT fragment this)
  shadow/       nwb_render_shadow : HW RayQuery + SW compute traversal + hybrid + ShadowState/Targets
  softshadow/   nwb_render_softshadow : downsample/reproject-merge/atrous/transparent-fold
  caustics/     nwb_render_caustics   : emission-targets/producer sw+hw/accumulator-decay/resolve
  gi/           nwb_render_gi     : surfel spawn/hashbuild/trace/resolve + pool/cellhead/counter
  csg/          nwb_render_csg    : csg_interval_peel.cpp(1150) split into detail/resources/dispatch
  avboit/ skinning/ (relocated IRenderPass) / ui/ (relocated IRenderPass)
```

--------------------------------------------------------------------------------
## 3. God-file split map (verified seams)

- **raytracing_system.cpp (7354)** → `swbvh/` (LBVH build/refit + CPU scene/instance BVH + HW TLAS +
  `SwBvhTraceResources`, ~L105-160,481-563,704-1138,2791-2874,5392-5998) · `shadow/` (~L1266-1910,
  2334-2651,2875-3496,6031-6147) · `softshadow/` (~L2080-2334,3951-4968) · `caustics/` (~L1138-1266,
  1685-1803,1946-2080,2659-2791,3496-3951,4968-5392) · `gi/` (~L6168-6890) · `swbvh/src/bvh_selftest.cpp`
  (NWB_DEBUG, ~L6899-7354) · `kernel` thin dispatch table + hasWork gates. Each feature owns its
  `<Feature>State` and its own RT targets (move RT textures OFF `DeferredFrameTargets`).
- **api.h (3358)** → `rhi/{primitives,format,resource,shader,pipeline_state,binding,raytracing,command,
  coopvec,device,hash}.h` behind a thin `api.h` umbrella; the stateful `GpuCrash*` service moves OUT of
  the pure-POD desc header into `rhi_services/graphics_crash.cpp`.
- **vulkan/backend.h (2411)** → `src/{detail,memory,resources,shaders,bindings,pipelines,commandlist,
  state_tracker,queue,raytracing,queries,device}.h` behind a thin umbrella. The four >2100-line
  partial-class `.cpp` split by class-owner: texture → {resource,commands,helpers,sampler};
  raytracing → {accel_struct,rt_pipeline_sbt,rt_commands,cluster_coopvec}; backend_context →
  {instance,device_create,swapchain,platform/surface_win32}; resource_bindings →
  {binding_layout,binding_set,descriptor_table,descriptor_heap}.
- **material cook.cpp(3071)+bind.cpp(1779)** → `material_parse` / `material_bind_codegen` (the .bind→Slang
  generator, ~900 lines — a compiler, not metadata) / `material_dispatch_codegen` / `material_generated_shaders`
  / `bind_parse` + `bind_typed_layout` / thin `cook.cpp` orchestration. `asset.h` stays the ONE sanctioned
  `#if NWB_COOK` mixed header.
- **filesystem/module.cpp (2137)** → `volume.h` + `volume_{format,io,publish,filesystem,session}.cpp`.
- **vector.h (3484)** → `math/simd/backend.h` (the 148-`#ifdef` intrinsic seam) + `vector_ops.h` +
  `vector_transcendental.h` + thin `vector.h` umbrella.
- **shader cook.cpp (1525)** → `shader_compiler_process.cpp` (the ONE platform `#ifdef`, slangc subprocess,
  behind a clean interface) + `shader_dependency_scan` + `shader_meta_parse` + variant/checksum `cook.cpp`.
- **metascript parser.cpp (1280)** → `parser_core.cpp` (schema-agnostic grammar) + `metascript_asset_bind.cpp`
  (the asset.structs/instances sublanguage) + `document.cpp`.
- **renderer_state.h (771)** → per-feature `<Feature>State` headers in each slice's `src/`;
  `DeferredFrameTargets` → `GBufferTargets` + per-feature target structs, each with its own `valid()`.

--------------------------------------------------------------------------------
## 4. Anti-piling rules (the core ask: "so source got too big")

These are the enforceable conventions that stop re-piling:

1. **Physical public/private split** — private headers live in `src/`, unreachable across the target
   boundary. A god-struct like `renderer_state.h` can no longer be *reached*, so behavior can't pile through it.
2. **The spine is feature-blind** — nothing in foundation..host may name a feature (no `if GI`, no RT
   texture on a shared target, no feature enum in the RHI). A new render feature adds ZERO lines below `host/`.
3. **One feature = one directory = one target** (render tier) — its GPU state never lives in a shared
   header. There is nowhere else for the feature to accrete.
4. **Features never friend each other** — cross-feature data flow only through public headers or the
   published `contract/` ABI. This outlaws the all-friends renderer clique and the
   `deferred_lighting → RT-state` friend write-back (becomes an explicit published render-graph input).
5. **Mega-headers banned; umbrellas are thin re-include shims only** — no new declaration may be ADDED
   to `api.h`/`backend.h`/`vector.h`/`global.h`; new code includes the narrow per-concern header.
6. **Cook is a link-level partition, not `#ifdef` scatter** — a runtime library's compiler never sees a
   cook header. The SOLE sanctioned mixed file is each asset type's `asset.h` (`#if NWB_COOK serialize()`).
7. **One name meaning per basename** — `module.h` is abolished (`asset_codec.h`, `volume.h`, `graphics.h`,
   `frame.h`). No two headers in different tiers share a name.
8. **No catch-all TU** — a `.cpp` mixing >2 concern-owners, or crossing ~800 lines, is a review smell that
   a slice is forming a god-file. Split at the seam; name each `.cpp` `<owner>_<concern>.cpp`.
9. **CI grep gate** (not a custom CMake DSL): no upward includes; no `volk` symbol in any `impl/` TU.

--------------------------------------------------------------------------------
## 5. Sequenced plan — leverage-first, risk-aware (THE operative section)

The adversarial critique's central correction: **sequence by leverage, not by tree position.** ~80% of
the "neatness" comes from a handful of *additive, umbrella-preserving* header splits + the pimpl — these
barely touch git history and don't collide with feature work. The disruptive placement surgery
(especially the `raytracing_system.cpp` shatter) is **deferred until the in-flight features land**,
because splitting the single most volatile file in the repo mid-feature = guaranteed merge hell +
destroyed `git blame` on the exact files under active development.

### Phase A — DO NOW (safe, additive, high-leverage; no file moves, no namespace churn)
1. **Pimpl the Graphics facade** — hold the backend behind a forward-declared opaque pointer; confine
   `backend_selection.h`/`volk` to one facade `src` TU. Verify `volk` no longer reaches any `impl/` TU;
   verify no per-draw frame-time regression (keep hot command recording on the concrete inlined path).
   *#1 leverage in the codebase, contained to the graphics module.*
2. **Explode `api.h`** into `rhi/*.h` behind a THIN `api.h` umbrella (every existing include keeps
   working); move `GpuCrash*` out of the POD desc header. Bank the win by migrating a few hot render TUs.
3. **Split `log.h`** → thin facade (`LogType`/`ILogger`/macros) + `log_detail.h` (~30 `EnqueueMessage`
   overloads), so 104 INFO/WARNING-only consumers stop instantiating the diagnostic-capture set.
4. **De-fat `global/containers.h`** (drop the fs/name/sync re-exports that force `<windows.h>`+`Name`+TBB
   into every `Vector<T>` user); extract `math/simd/backend.h` + split `vector.h` behind its own umbrella.
5. **Shatter `vulkan/backend.h`** into per-concern headers behind an umbrella — bounded to the backend
   domain; stops ~28 TUs recompiling on any private-member edit.
6. **Carve `nwb_thread` out of `nwb_alloc`** (thread/job/affinity), leaving `using`-aliases in the `Alloc`
   namespace; switch `World` to *compose* a `ThreadPool&` instead of inheriting `ITaskScheduler`.
7. **Apply include/(public)+src/(private)** to the leaking modules only: graphics/RHI, alloc/thread, log,
   volume. NOT the reference-clean ecs/perf/telemetry (no debt there — churn for nothing).

### Phase B — DEFER until surfel GI U1–U6 and caustics P5/P6 are committed to main
8. **Shatter `raytracing_system.cpp`** into `swbvh` + `shadow` + `softshadow` + `caustics` + `gi` targets
   along the verified line-ranges; split `renderer_state.h` + `DeferredFrameTargets` into per-feature
   state/target headers; invert the `deferred_lighting` friend write-back into a published input.
   *Highest-risk move — genuine design work (lifetime/resize/publish ordering), not mechanical. Do LAST.
   Gate each feature split on a forced-emu SW==HW byte-identical capture + validation/logger-clean across
   dbg/opt/fin.*
9. `impl/ecs_render/` → `impl/render/<feature>/` directory reorg; lift `TransformComponent` → `nwb_spatial`;
   relocate skinning + ui (both `IRenderPass`).
10. Cook partition: extract `nwb_assets_cook`; mark `nwb_metascript` cook-tier (grep-verify NO runtime
    path parses `.nwb` first); split material/shader cooks; make material→shader cook order a typed pipeline.
11. Periphery: `nwb::runtime_asset_codecs` / `nwb::cook_asset_codecs` aggregators (delete the triplicated
    WHOLE_ARCHIVE lists); retire the temporary umbrella shims once consumers include narrow headers.

### Mechanical rules for every move (protect the solo-dev archaeology)
- **`git mv` in a content-free commit, then split content in the FOLLOWING commit** — never move-and-edit
  in one commit, or `git blame`/`--follow` breaks on files you alone hold in your head.
- **CRLF**: verify `.gitattributes` (`* text eol=crlf`) before any bulk-create; create stubs then `Edit`
  (Edit preserves CRLF; the Write tool emits LF). Prefer in-place splits (fewer new files).
- **Arena Names**: move each alloc site's collected constexpr `Name` with its code into a per-module
  `arena_names.h`; grep for duplicate `Name` symbols post-split (ODR); keep companion source-text strings
  in every codegen path (never `Name::c_str()` — opt `0x80000003` trap).

--------------------------------------------------------------------------------
## 6. Must-preserve invariants (the reorg must NOT break)

- Namespaces frozen: `NWB` (foundation) / `NWB::Core` (substrate..host) / `NWB::Impl` (impl). Only
  additions: `NWB::Core::Thread` (carved from Alloc, with a `using`-alias shim) and optionally
  `NWB::Render::<Feature>`. **Do NOT retire `NWB::Impl` or mass-rename** (rejected: whole-codebase churn,
  zero behavioral gain).
- Arena Name convention (Name-first ctor, collected constexpr Name per site, never a string literal).
- `Name::c_str()` is a debug-only 136-char hash — codegen uses companion source-text strings.
- Cook double-compile gating: `asset.h`'s `#if NWB_COOK serialize()` is the ONE sanctioned mixed header;
  the runtime image never links cook code or metascript; keep the FULL-variant-only cook toolchain.
- Shader include direction (project shaders include engine shaders); engine `.nwb` = `include`, project
  `.nwb` = `shader`. **`binding_slots.h`/`names.h`/`constants.h` are dual-consumed by Slang AND C++ —
  do NOT move them**; treat as a published cross-tier contract in place.
- Material→shader cross-asset cook ordering (AssignShadingModelIds → EmitBxdf/ShadowTransmittance dispatch
  → PrepareShaderEntriesForCook) preserved (make it a typed pipeline, never reorder).
- The compile-time, **zero-vtable, single-backend** RHI seam (`api.h` forward-declares concrete Vulkan
  classes). Explode `api.h` for compile decoupling ONLY — **do not** introduce a vtable/portability
  abstraction under a "second backend" banner (speculative generality; the engine mandates one backend).
- CRLF everywhere; Clang ≥19; dbg/opt/fin on FULL windows-clang-x64.
- The skinning-pass write into the RT triangle-attribute buffer (so shadow/caustic traces bend on the live
  pose) keeps functioning even if the pass relocates.
- Verification bar every step: forced-emu SW==HW byte-identical capture parity, Vulkan-validation-clean,
  logger-warning-clean; `nwb_assets_graphics_tests` 40/40; pipeline-cache fixed-name Volume behavior.

--------------------------------------------------------------------------------
## 7. Rejected (over-engineering)

- **Whole-tree feature-slicing** (geometry/scene/shadercook as "features"): they are infrastructure, not
  features. Slice the RENDER tier only.
- **Custom CMake layer-enforcement DSL**: the graph is already grep-clean; a one-line CI grep locks it in.
- **Fragmenting shared render substrate** (sw-bvh, G-buffer targets) into "features others consume by
  interface": they are legitimately shared — keep `render_core` + `swbvh` as explicit shared libs.
- **A new render/kernel pass-graph sequencer**: that is a behavioral subsystem, not a placement move.
- **include/src on the clean ecs/perf/telemetry**: spend the discipline only where leakage is proven.

================================================================================
Provenance: 22-agent workflow (14 domain mappers → coupling/decomp/build analyses → 3 proposals
[Strata/Sliced/Ledger] → synthesis "Keystone" + adversarial critique). This doc = the synthesis's
target end-state tempered by the critique's sequencing/scope discipline for a solo dev mid-feature.
