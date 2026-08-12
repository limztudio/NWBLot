# GPU task-graph redesign acceptance audit

**Decision:** conditional closeout; **not** strict final architecture acceptance.

**Reviewed baseline:** `c62605d9` (`Fix acceptance policy regressions`), 2026-08-11.

**Follow-up implementation:** physical queue identity, frontier-safe deferred scheduling, graph-bound presentation,
graph-owned per-frame ImGui uploads, opt-in ready-frontier native recording, and opt-in same-class physical Graphics
routing, actual-device stale packet-recording recreation coverage, qualified dedicated-Transfer recovery-frontier
coverage, graph-owned public setup uploads,
graph-owned deferred mesh-view, scene-light, and scene-shading frame updates, and graph-owned decoded texture-asset
uploads, graph-owned runtime-skinning dispatch packets, graph-owned opaque material draw-stream uploads, and
graph-owned opaque CSG receiver/cutter/context/interval streams, graph-owned transparent CSG interval-producer
streams, graph-owned transparent AVBOIT occupancy, extinction, and accumulation material/CSG streams, and
graph-owned AVBOIT final G-buffer state handoffs, graph-owned normal Shadow Visibility hardware/software traversal
entry-state handoffs, graph-owned normal Software Caustics entry-state handoffs, graph-owned CSG clip-buffer entry-state handoffs, and graph-owned current and lagged deferred bindless-selector, ray-trace material-context
selector, caustic
emission-target, surfel-frame constant, shadow material-context batch and retained hybrid hardware fallback context,
software-only and hybrid scene-BVH pair uploads and healthy-hybrid frozen software traversal tables, software-only
and hybrid per-mesh SW-BVH build/refit,
opaque and healthy hybrid hardware TLAS, and opaque and hybrid hardware BLAS build transactions,
2026-08-12.

The task/resource-graph migration is accepted as the current bounded renderer architecture: graph declaration owns
semantic dependencies, compile-time queue selection, packet construction, and inter-task barrier planning; native
recording remains the ordinary path; and optional command IR is tooling-only. This decision does not certify every
completion criterion in the original redesign proposal. It records the exact work that remains before that proposal
can receive an unconditional final sign-off.

## Accepted scope

- The legacy `FrameExecutionPlan` family is absent; the deferred renderer builds a shared task graph and consumes
  compiler-derived packet ranges.
- Task IDs, resource uses, inferred hazards, deterministic queue selection, packet generation, graph state seeds,
  and barrier/ownership planning are implemented and covered by focused tests.
- Accepted/rejected packet lifecycle is transactional. Native recording is still the default runtime path; command IR
  capture, validation, replay, and the direct-Vulkan `CopyBuffer` lowerer are opt-in tooling.
- Vulkan-only descriptor-buffer bindless rendering remains fail-closed. The retained `RenderLane` RHI compatibility
  facade is not used to restore renderer scheduling.
- Large setup uploads have an automatic Transfer-preferred route with Graphics/Compute fallback. The external
  dedicated-Transfer performance proof is explicitly deferred by the user for this closeout.
- Representable synchronous `Graphics::setupBuffer` and `Graphics::setupTexture` payloads now declare a one-packet
  graph upload: caller bytes are retained as immutable graph blobs, the built-in buffer/texture task publishes the
  graph-visible final state and accepted producer token, and the pre-existing consumer-queue readiness bridge preserves
  the returned-handle compatibility contract. The direct path remains a narrow fallback for combined depth/stencil
  or unknown keep-initial-state descriptors until specialized declaration-safe tasks exist.
- Texture assets decode every UASTC mip/slice into immutable batch regions before graph declaration. The batch helper
  retains each region as a graph upload blob, serializes the subresources to one terminal accepted token, declares
  `ShaderResource` as the visible final state, and keeps the automatic Transfer -> Compute -> Graphics route plus
  consumer readiness bridge. Bindless sampled-image allocation/commit follows only after that graph batch accepts.
- Runtime skinning now derives immutable per-mesh dispatch plans before recording. Active joint palettes and one-time
  bindless selectors upload into the same primary-Graphics graph packet as deformation, meshlet-bounds, and optional
  normal-repack compute work; no-active poses similarly merge their three-region rest-to-skinned copy with the
  bounds/repack continuation. The graph-owned compute task consumes selectors as `ConstantBuffer` (automatic
  tracking restores their cross-frame `Common` state), publishes every renderer-visible output as `ShaderResource`,
  and commits selector residency plus dirty-state changes only when the packet accepts. Releasing a pose still forces
  one bind-pose copy/repack.
- In the shared deferred path, ImGui vertex/index payloads and requested font/texture updates are retained as
  immutable graph blobs, lowered through the built-in upload tasks, and made dependencies of the terminal overlay
  or upload-completion packet. Small deltas stay on Graphics; amortizable updates prefer Transfer.
- The shared deferred graphics prefix resolves changed mesh-view, scene-light, and scene-shading data before graph
  compilation. Each payload is retained as an immutable graph blob, uploaded through a Graphics-routed built-in
  buffer task, and committed to its CPU mirror only when the matching packet is accepted. These automatic-state
  buffers return to `Common` at packet close; their first declared consumer owns the transition to
  `ConstantBuffer` or `ShaderResource`.
- The deferred G-buffer clear is now an explicit serial bundle of five built-in Graphics clear tasks (albedo,
  normal, world position, depth, and terminal opaque color). The first and terminal task bracket the existing
  deferred-clear timing scope inside their native clear commands, and recording rejects a compiled route unless
  both endpoints share the same Graphics packet; the opaque-color task therefore remains the final owner of the
  later asynchronous handoff.
- A target generation's current deferred bindless selector now follows the same acceptance-safe graph path. Its
  immutable slot payload is retained before declaration and, when not yet resident, a tiny Graphics-routed built-in
  upload precedes and merges into the first Shadow Preparation packet. Its automatic retained state is
  `ConstantBuffer`, matching the descriptor-visible state at every native packet close; the graph's declared
  selector use and packet handoffs therefore need no normal-frame native state bridge. That packet alone commits
  `slotsUploaded` on acceptance, and graph-recorded Deferred Lighting, Composite, and Present use the declared
  selector instead of re-reading or rewriting mutable CPU slot data. The direct helper remains for non-graph
  compatibility callers. Active lagged Lighting now retains its history selector as a separate immutable graph
  blob immediately before Deferred Lighting, pinned and merged into that dedicated Compute packet. Its automatic
  selector still publishes `Common`, then Lighting owns the `ConstantBuffer` transition and its existing external
  prior-history completion wait. The existing Lighting acceptance callback alone commits the history residency bit,
  so rejected packets retry the same graph-owned upload without a stale CPU-side commit.
- Ray-trace material-context selector slots are resolved only after Shadow Visibility preflight has frozen every
  backing buffer and descriptor slot. The immutable 32-byte payload is retained as a graph blob and uploaded to
  `Common` immediately before, and merged into, Shadow Preparation. Shadow Preparation retains the logical
  `ConstantBuffer` write handoff so later Compute trace consumers wait on the accepted first Graphics packet rather
  than an upload frontier. This per-frame selector has no residency bit: packet rejection discards the preflight
  plan and the next graph build resolves and retains a fresh payload. The native direct writer remains only for
  non-graph compatibility callers.
- The normal deferred Shadow Visibility task now declares its descriptor-visible hardware and software traversal
  entry states: sampled G-buffer inputs (including depth as `ShaderResource`), selector and scene constant buffers,
  traversal/material streams, TLAS/backing storage, and its output/scratch UAVs. Its graph callback no longer
  reissues those entry transitions. Direct or minimal callers retain native setup by default; the preflight-failure
  `CopyDest` clear and all intra-task soft/hybrid UAV fences remain native ownership.
- The normal deferred Software Caustics task now consumes graph-declared descriptor-visible shared inputs: sampled
  world/depth textures, bindless/scene/material/mesh constant buffers, emission and frozen software-BVH traversal
  streams, plus scene/material/light buffers. Its depth use is `ShaderResource`, matching the bindless sampled-image
  shader read; the callback no longer restates that entry batch. Direct callers retain native setup by default, while
  caustic target clears, temporal accumulator reset/decay, and the multi-pass resolve/UAV sequence remain native
  task-local ownership.
- Caustic preflight now freezes the exact refractive-instance AABB stream after it has selected buffer capacity and
  descriptor residency. A nonempty immutable blob uploads after the material-context selector and merges into the
  same Shadow Preparation packet, publishing automatic-state `Common`. Shadow Preparation keeps the logical
  `ShaderResource` write handoff so software and hardware caustic consumers wait on that accepted packet rather
  than the upload frontier. Empty preflight snapshots authoritatively skip both the upload and record-time regather;
  rejection discards the preflight snapshot and the next build gathers fresh bytes. The native writer remains only
  for non-graph compatibility callers.
- Surfel GI now freezes its 80-byte per-frame constant payload—frame index, round-robin divisor, pool dimensions,
  and render extent—before graph recording. An active frame retains a tiny immutable `Common` upload after the
  caustic stream and merges it into Shadow Preparation, which owns the logical `ConstantBuffer` handoff to the
  later asynchronous GI work. There is intentionally no residency bit: a rejected packet rebuilds the immutable
  payload from the preserved frame state, while the later Surfel GI path remains responsible for frame progression.
  The direct writer remains only for non-graph compatibility callers.
- Shadow-trace material context now freezes its ABI-coupled instance-material table, instance records, and typed
  material byte stream together before recording. For hardware-only and forced-software routes, a complete
  all-or-nothing immutable triple chains into Shadow Preparation and publishes automatic-state `Common`; Shadow
  Preparation retains the logical `ShaderResource` writes so later Compute trace work waits on the accepted first
  Graphics packet. A healthy hybrid transparent hardware-to-software frame now publishes the final
  software-compatible triple too: hardware TLAS recording leaves it untouched, and the retained hybrid traversal
  table restores its matching descriptor context without a recording-time CPU scene/material regather. The common
  accepted callback commits the SW static cache. A traversal-table miss takes the established direct revalidation
  boundary; if that optional tail cannot record, Shadow Preparation clears the frozen triple and restores the
  preflight-retained immutable hardware context before accepting opaque HW shadows. That restore validates its
  storage identity, descriptor handles, capacities, and source mutation versions; only a stale snapshot takes the
  established current direct-hardware retry. If restoration still fails, it disables only the material-context
  consumers (transparent fold, caustics, and surfel GI) for that frame. The static cache commits only after Shadow
  Preparation accepts; rejection and record-time mismatch discard the frozen plan.
- The CPU-built software scene-BVH now freezes its node records and leaf-instance stream as one immutable pair on
  the software-only route and the independent hybrid software tail. The node leaf ranges address that exact instance
  stream, so both `Common` uploads chain into Shadow Preparation and are handed off there as `ShaderResource` to the
  later Compute shadow, caustic, and Surfel consumers. Its static-scene cache commits only from the accepted
  preparation packet. Healthy hybrid preflight also retains one distinct-mesh traversal table with owning mesh
  buffers, descriptor slots, byte sizes, primitive counts, runtime versions, and relevant ECS mutation versions.
  Shadow Preparation restores that table rather than rebuilding CPU scene/material data. A table, resource, or
  version miss takes direct revalidation; a failed optional tail discards only that pair and preserves the valid
  hardware opaque result. The healthy hybrid TLAS build retains its own immutable instance plan and narrow direct
  retry boundary.
- Software-only per-mesh SW-BVH builds and refits now freeze their selected mesh inputs, node/parent buffers,
  descriptor slots, bounds, rebuild/refit decision, and the shared sort/payload/counter scratch generation before
  graph compilation. They record serially inside Shadow Preparation; parent links and shared scratch stay in their
  true `UnorderedAccess` state and join the accepted cross-frame state seed. Mesh topology/refit flags commit only
  after that state handoff accepts. Hybrid frames now use that frozen plan for the independent per-mesh SW-BVH
  portion too: if it cannot record, the plan is discarded and the valid hardware opaque result still submits.
  Healthy hybrid scene construction now freezes independent TLAS and software traversal plans as well, while a
  plan mismatch retains the direct retry boundary and the same state-only imports preserve UAV producer state across
  a later route switch.
- Opaque and healthy hybrid hardware TLAS builds now retain exact preflight instance descriptors together with the
  referenced BLAS, selected TLAS, and backing-buffer handles. The native build stays inside the existing Shadow
  Preparation Graphics packet, explicitly transitions acceleration structures from build-write to read, and commits
  its static cache and cross-frame backing-state seed only after that packet accepts. A hybrid capture or record-time
  mismatch discards only the frozen TLAS plan and retries the established direct build, preserving the graph-owned
  software-compatible material context and the valid hardware opaque fallback.
- Hardware BLAS builds and refits now freeze the selected static or runtime mesh operation with its exact
  position/index buffers, BLAS, backing generation, and rebuild/refit decision. The plan records before the frozen
  TLAS in the same Shadow Preparation packet, and every live BLAS backing participates in the accepted cross-frame
  state seed. Hybrid frames now use this independent frozen per-mesh work too: a mismatched or failed frozen record
  discards the plan and retries the established direct loop, preserving the valid opaque-HW fallback. Pending-build
  and refit counters commit only after an accepted frozen handoff; transparent hybrid TLAS scene construction retains
  its direct retry boundary only when its independent frozen plan cannot be used.
- The opaque G-buffer now freezes its material draw ordering and CSG CPU frame payload during graph declaration.
  Its instance and typed-material bytes are retained as immutable graph blobs and uploaded through Graphics-routed
  built-in buffer tasks after deferred clear. The tasks publish the buffers' automatic `Common` close boundary; the
  G-buffer declares the transient `ShaderResource` reads. CSG receiver-range, cutter, clip-context, and interval
  sample bytes from that same frozen payload are immutable graph blobs, uploaded in order before the G-buffer. The
  graph declares the receiver/cutter `ShaderResource` reads and the context/sample plus target-generation deferred
  bindless-slot `ConstantBuffer` reads. Its two persistent CSG interval values (`csgIntervalId` over all peel
  layers and slice-zero `csgReceiverEventCount`) now clear through a frozen-rect graph task immediately before the
  opaque native producer, then explicitly hand off from `CopyDest` to `UnorderedAccess`. All three peel arrays
  (`csgCapBackNormal`, `csgIntervalDepth`, and `csgIntervalId`), receiver-surface event and receiver-span images,
  and removed-interval outputs now declare their exact `UnorderedAccess` ranges before normal opaque and
  prepared-transparent thunks record, so those producers no longer stage their initial StorageImage states natively.
  The native receiver-span event-image and interval-combine input UAV fences remain inside their aggregate native
  task, while opaque post-combine material/cap sampling now records in a mergeable graph task that declares the four
  removed-interval aliases as `UnorderedAccess` reads and lowers their required UAV handoff. Transparent AVBOIT
  sampling, wider CSG target lifecycle, and direct compatibility clear remain deliberately outside this bounded task.
  The transparent CSG interval producer now does the same within AVBOIT-pre:
  it freezes its fresh mesh-view work region, receiver-surface draw ordering, material instance/typed data, and CSG
  receiver/cutter/context/sample payloads during graph declaration. Its serial Graphics upload chain merges into
  the established AVBOIT-pre packet, which consumes the graph-owned bytes before later AVBOIT phases overwrite
  their shared buffers. Its prepared path also uses the same graph-owned paired rect clear before native interval
  production, while an unprepared compatibility path retains the direct helper. The subsequent transparent
  occupancy phase now freezes its own material instance/typed stream and, when it contains CSG draws, its
  receiver/cutter/clip-context stream. Its serial Graphics uploads run
  after interval generation and merge into that same AVBOIT-pre packet; it deliberately retains the interval
  producer's full-resolution sample-state payload for low-resolution sampling. The prepared native occupancy
  consumer never regathers or rewrites those streams, and the renderer verifies that its task is in the interval
  task's accepted packet. When both prepared CSG interval production and prepared occupancy CSG sampling are
  present, occupancy also declares the four removed-interval aliases as exact-range `UnorderedAccess` reads, so
  the compiler lowers their same-packet UAV handoff before its native thunk records. Direct occupancy retains its
  native sample-state bridge. The following extinction phase freezes a
  separate material instance/typed stream and,
  for CSG draws, receiver/cutter/clip-context data; it intentionally reads rather than rewrites the interval
  sample state. Its prepared CSG consumer also declares the same four exact-range `UnorderedAccess` reads: on
  Graphics-only frames the handoff remains in the AVBOIT-pre packet, and on the split route the compiler preserves
  four state seeds and waits for both the interval-producer and depth-warp packets before the Graphics extinction
  thunk records. Direct and unprepared extinction paths retain the native bridge. On Graphics-only frames its
  upload chain and native post-occupancy tail merge into the same AVBOIT-pre packet; on the split route they merge
  into the existing Graphics extinction packet after depth warp.
  The final accumulation phase freezes its own material instance/typed payload and, for CSG draws,
  receiver/cutter/clip-context payload after integration; it also retains the interval producer's full-resolution
  sample state as a read-only input. When both prepared interval production and prepared accumulation CSG sampling
  are present, accumulation declares the same four exact-range `UnorderedAccess` reads. On the split route their
  source state seeds survive the depth-warp, extinction, and integration packets and add the required producer
  wait before accumulation records; direct and unprepared accumulation paths retain the native bridge. Its serial
  Graphics upload chain and prepared consumer merge into the terminal Graphics AVBOIT packet—after integration on
  the split route and after the Graphics-only post-extinction tail on the normal route. A following mergeable
  Graphics finalizer declares the two accumulation attachments as
  `ShaderResource` before Deferred Composite consumes them, while the read-only depth attachment retains its
  explicit compatibility handoff. Runtime checks require every phase stream and finalizer to share its native
  consumer's accepted packet and keep the AVBOIT range at one or five packets. Direct helpers remain only for
  non-graph compatibility callers. Across opaque G-buffer/interval sampling and prepared transparent interval,
  occupancy, extinction, and accumulation callbacks, those exact graph declarations now also own the CSG
  receiver/cutter `ShaderResource` and clip-context/interval-sample `ConstantBuffer` entry states. The native
  helper remains for direct, unprepared, or empty gathered-work paths; the receiver-surface-to-span and
  span-to-combine task-local UAV fences remain native.
- The compiler derives stable packet recording-frontier depths from packet dependencies. The native recorder may
  record an explicitly opted-in frontier concurrently with isolated per-packet state scratch and native command
  buffers; submission, timing, and failure reporting remain in stable compiler order. Command-IR capture and
  legacy external-state overrides deliberately retain serial recording.
- A Vulkan device may opt into one auxiliary Graphics `VkQueue` from the selected Graphics family. The Device
  registry assigns it a distinct physical-queue identity; only graph tasks that explicitly allow same-class routing
  may use it. The compiler uses deterministic current-cost balancing within that family, broad
  `CommandQueue::Graphics` callers remain on the primary queue, and a same-family handoff uses the graph packet's
  timeline dependency without emitting a queue-family ownership transfer.
- Recorded graph and submission-transaction state is generation-bound. Recompiling after graph reset invalidates
  stale recording state until it is reset for the new compiled graph, releasing the old packet-owned command-list
  handles before recording can resume.
- A late recovery packet is an independent Graphics tail that derives one current-generation timeline wait from
  every accepted non-destination physical queue. The transaction-level test covers Compute and Transfer sources;
  the dedicated-Transfer headless smoke accepts a graph-owned Transfer upload, rejects its dependent Graphics
  suffix, then late-records and submits the recovery tail. A test-only Device-boundary capture verifies that the
  submitter supplied the exact Transfer token to that native submission. The dedicated-Transfer half is
  topology-gated and skips only when the adapter lacks a dedicated Transfer-only family.

## Verification performed

| Check | Result |
| --- | --- |
| Serial rebuild of graph, descriptor-buffer smoke, Transfer-profile, and command-IR-profile targets | passed |
| Recompile of `impl/ecs_render/kernel/system.cpp` | passed |
| `ctest -C dbg -R '^(nwb_graphics_task_graph_tests|nwb_descriptor_buffer_tests)$'` | 2/2 passed |
| `graphics_task_graph_tests` | 37/37 passed |
| `descriptor_buffer_tests` | 67 passed; 9 expected skips because this host has no dedicated Compute-only or Transfer-only family |
| Project policy checks (`return_value_handling`, test `std::` use, interop containers) | passed |
| Command-IR and Transfer A/B launcher/runner self-tests | passed |
| Command-IR profile, 4,096 records, Vulkan validation | passed; capture remained allocation-free and direct `CopyBuffer` replay was 3.63% faster than `Core::CommandList` replay |
| Physical queue identity follow-up: graph unit binary | 37/37 passed |
| Physical queue identity follow-up: descriptor-buffer smoke binary | 68 passed; 9 expected skips because this host has no dedicated Compute-only or Transfer-only family |
| Retired-device submission token | verified: graph bindings reject a stale generation, and Vulkan rejects it at the native submission boundary |
| Graph-owned ImGui setup-upload follow-up: `graphics_task_graph_tests` | 42/42 passed, including small Graphics-retained and large Transfer-preferred terminal upload routing |
| Graph-owned ImGui setup-upload follow-up: `descriptor_buffer_tests` | 71 passed; 9 expected skips because this host has no dedicated Compute-only or Transfer-only family |
| Ready-frontier recording follow-up: graph-unit binary | 43/43 passed, including deterministic compiler-derived frontier depth |
| Ready-frontier recording follow-up: descriptor-buffer smoke binary | 72 passed; 9 expected skips because this host has no dedicated Compute-only or Transfer-only family; two independent immutable uploads recorded through a worker frontier and submitted in compiler order |
| Ready-frontier recording follow-up: `nwb_ecs_ui` and `nwb_ecs_render` | passed |
| Same-class Graphics routing follow-up: graph-unit binary | 45/45 passed, including deterministic opt-in balancing, same-family state planning, duplicate native queue rejection, and stale recording/transaction recreation |
| Same-class Graphics routing follow-up: descriptor-buffer smoke binary | 72 passed; 10 expected skips. The added real-Vulkan route skipped because this adapter exposes only one Graphics queue; the existing 9 skips lack dedicated Compute-only or Transfer-only families |
| Actual device-recreation graph-packet follow-up: descriptor-buffer smoke binary | 73 passed; 10 expected skips. A real headless Graphics instance releases its recorded packets/transaction before teardown, recreates its device, then recompiles, records, and submits only on the replacement generation |
| Graph-owned public setup-upload follow-up: descriptor-buffer smoke binary | 73 passed; 10 expected skips. Normal buffer and texture setup routes compile, record, and submit through one graph packet while preserving queue selection, graph-visible final states, and Graphics readiness; the dedicated-Transfer probe is hardware-skipped on this adapter |
| Graph-owned deferred-frame upload follow-up: `nwb_ecs_graphics_tests`, `nwb_graphics_task_graph_tests`, and `nwb_descriptor_buffer_tests` | 3/3 passed |
| Graph-owned deferred-frame upload follow-up: rebuilt `nwb_testbed` window capture | passed; a real X11/Vulkan deferred frame completed with the graph-owned mesh-view, scene-light, and scene-shading uploads |
| Graph-owned decoded-texture upload follow-up: `nwb_assets_texture_loader`, `nwb_graphics_task_graph_tests`, and `nwb_descriptor_buffer_tests` | passed; the native smoke now reads back two graph-owned mip payloads after their caller arrays are overwritten (74 passed; 10 expected topology skips) |
| Graph-owned skinning-dispatch follow-up: `nwb_ecs_mesh_skinning`, `nwb_ecs_graphics_tests`, graph/descriptor smoke, and runtime skinning smoke | passed; one primary-Graphics graph packet combines selector/palette uploads, rest-stream copies, deform, bounds, normal repack, and acceptance-only CPU commits without a native continuation. 18/18 ECS unit tests, 50/50 graph tests, and descriptor smoke 77 passed with 11 expected topology skips; the one-character animated 339-joint Vulkan fast smoke completed its warm-up and sampled frames. |
| Graph-owned opaque material-stream follow-up: `nwb_ecs_graphics_tests`, graph/descriptor smoke, and runtime skinning smoke | passed; 17/17 ECS unit tests, 45/45 graph tests, descriptor smoke 74 passed with 10 expected topology skips, and the one-character Vulkan smoke completed its animated opaque-material case |
| Graph-owned opaque CSG complete-stream follow-up: static/compute opaque capture plus transparent CSG regression captures | 5/5 passed; both opaque Vulkan/X11 paths consumed graph-owned receiver, cutter, context, and interval-state buffers before Opaque G-Buffer, while all three transparent compatibility captures remained correct |
| Graph-owned transparent CSG interval-producer follow-up: ECS graphics unit plus static and skinned transparent CSG early/mid/late captures | 7/7 passed; the frozen receiver-surface payload is uploaded before AVBOIT-pre while the existing one/five-packet AVBOIT contract and visible transparent CSG cuts remain intact |
| Graph-owned transparent AVBOIT occupancy-stream follow-up: ECS graphics, task-graph, descriptor smoke, transparent multi, plus static/skinned transparent CSG captures | 10/10 passed; interval generation, phase-local occupancy uploads, and prepared occupancy remain in one accepted AVBOIT-pre Graphics packet while the visible transparent and CSG cases stay correct |
| Graph-owned transparent AVBOIT extinction-stream follow-up: FrontierSafe task-graph topology, ECS graphics, descriptor smoke, transparent multi, plus static/skinned transparent CSG captures | 10/10 passed; phase-local extinction uploads merge with native extinction without adding a sixth async packet, while Graphics-only and CSG captures remain correct |
| Graph-owned transparent AVBOIT accumulation-stream follow-up: FrontierSafe task-graph topology, ECS graphics, descriptor smoke, transparent multi, plus static/skinned transparent CSG captures | 10/10 passed; phase-local accumulation uploads merge with the final native consumer without adding a sixth async packet, while Graphics-only and transparent CSG captures remain correct |
| Graph-owned current deferred bindless-selector retained-state follow-up: 51 task-graph, 18 ECS graphics, descriptor-buffer smoke, plus opaque and early/mid/late transparent CSG captures | passed; retained `ConstantBuffer` selector state is seeded into the graph packet handoff, removing Shadow Preparation's normal-frame native state bridge while preserving graph-owned upload acceptance |
| Graph-owned post-G-buffer ordinary-state normalization follow-up: 51 task-graph, 18 ECS graphics, descriptor-buffer smoke, plus opaque and early/mid/late transparent CSG captures | passed; compiler-owned declared states now lower the normalizer's ordinary G-buffer, scene, and descriptor transitions; only route-dependent trace geometry retains explicit compatibility normalization |
| Graph-owned AVBOIT target-clear follow-up: 51 task-graph, 18 ECS graphics, descriptor-buffer smoke, plus opaque and early/mid/late transparent CSG captures | passed; AVBOIT's nine target clears now declare their `CopyDest` writes in a distinct mergeable Graphics task before occupancy, preserving the shared Pre packet/timing contract while the full native helper remains for compatibility callers |
| Graph-owned AVBOIT occupancy-state follow-up: 52 task-graph, 18 ECS graphics, descriptor-buffer smoke, plus opaque and early/mid/late transparent CSG captures | passed; occupancy now consumes graph-declared depth `ShaderResource` and coverage `UnorderedAccess` states without a normal-frame native bridge. Focused compiler and real-Vulkan packet tests prove the clear-to-occupancy `CopyDest` to `UnorderedAccess` transition and the separate unsplit-tail UAV dependency; direct compatibility callers retain their bridge |
| Graph-owned AVBOIT accumulation-attachment state follow-up: 53 task-graph, 18 ECS graphics, descriptor-buffer smoke, plus opaque and early/mid/late transparent CSG captures | passed; a mergeable Graphics finalizer now lowers both accumulation attachments from `RenderTarget` to `ShaderResource` before Deferred Composite. Focused compiler and real-Vulkan packet tests prove that the finalizer shares Accumulation's packet and records without a native bridge; direct callers and the read-only-depth compatibility handoff remain intact |
| Graph-owned AVBOIT final G-buffer-state follow-up: 54 task-graph, 18 ECS graphics, descriptor-buffer smoke, lagged-lighting harness, plus opaque and early/mid/late transparent CSG captures | passed; normal graph recording no longer restores AVBOIT G-buffer inputs natively. Accumulation declares its read-only deferred depth as `DepthRead`, and the mergeable Graphics finalizer returns it and both accumulation outputs to `ShaderResource`. The active transparent frame-lagged dedicated-Compute route explicitly waits for that finalizer while its immutable selector upload remains independent; direct compatibility callers retain their native framebuffer-state bridge. |
| Graph-owned Shadow Visibility entry-state follow-up: renderer build, 59 task-graph tests, 18 ECS graphics tests, descriptor-buffer smoke, and GPU-validation hybrid A/B | passed; normal deferred Shadow Visibility now receives descriptor-visible sampled G-buffer inputs (depth specifically transitions `DepthWrite` to `ShaderResource`), constants, traversal resources, TLAS/backing storage, and output/scratch UAVs from graph declarations before its callback records. Focused compiler and real-Vulkan getter-only packet tests prove both producer handoffs and native-bridge-free entry. The short GPU-validation A/B passed healthy and forced-fallback arms (six timing intervals each) with no forbidden validation/runtime log. Direct callers, preflight clear fallback, and required intra-task UAV fences remain native. |
| Graph-owned CSG receiver-surface image-state follow-up: 54 task-graph, 18 ECS graphics, descriptor-buffer smoke, plus opaque and early/mid/late transparent CSG captures | passed; opaque G-buffer and prepared-transparent AVBOIT declare all receiver-event-data layers and the receiver-event-count layer as `UnorderedAccess` before their native material thunks record. The normal graph no longer manually transitions that StorageImage pair; unprepared and direct compatibility paths retain their bridge while the remaining CSG image lifecycle stays explicitly out of scope. |
| Graph-owned CSG interval-peel state follow-up: 54 task-graph, 18 ECS graphics, descriptor-buffer smoke, plus opaque and early/mid/late transparent CSG captures | passed; opaque G-buffer and prepared-transparent AVBOIT now declare every cap-back-normal, interval-depth, and interval-ID peel layer as `UnorderedAccess` before their native interval dispatches. The direct first-peel state bridge is removed only for those graph callers; compatibility paths and the native combine-stage UAV barrier remain intact. |
| Graph-owned CSG receiver-span output state follow-up: 54 task-graph, 18 ECS graphics, descriptor-buffer smoke, plus opaque and early/mid/late transparent CSG captures | passed; opaque G-buffer and prepared-transparent AVBOIT now declare every receiver-span-data layer and the receiver-span-count layer as `UnorderedAccess` before their native span dispatches. The graph removes only output setup; native event-image and span-to-combine UAV fences remain intact, as do direct compatibility paths. |
| Graph-owned CSG removed-interval output state follow-up: 54 task-graph, 18 ECS graphics, descriptor-buffer smoke, plus opaque and early/mid/late transparent CSG captures | passed; opaque G-buffer and prepared-transparent AVBOIT now declare every removed-interval depth/cap/data layer and the removed-interval-count layer as `UnorderedAccess` before their native combine dispatches. The graph removes only output setup; native combine-input and post-combine sampling UAV fences remain intact, as do direct compatibility paths. |
| Graph-owned opaque CSG interval-sample state follow-up: 55 task-graph, 18 ECS graphics, descriptor-buffer smoke, plus opaque mesh/compute and early/mid/late transparent CSG captures | passed; opaque interval combine now ends at a mergeable graph boundary, and the following opaque material/cap task declares all removed-interval outputs as exact-range `UnorderedAccess` reads. The compiler lowers four same-packet UAV handoffs before the sample thunk records; AVBOIT and direct compatibility consumers retain their native post-combine bridge. |
| Graph-owned AVBOIT occupancy CSG interval-sample state follow-up: 56 task-graph, 18 ECS graphics, descriptor-buffer smoke, plus static/skinned early/mid/late transparent CSG captures | passed; when both prepared transparent interval production and prepared occupancy CSG sampling are present, the four removed-interval outputs cross the intervening AVBOIT clear through compiler-lowered same-packet UAV handoffs. Direct occupancy paths retain their native sample-state bridge. |
| Graph-owned AVBOIT extinction CSG interval-sample state follow-up: 57 task-graph, 18 ECS graphics, descriptor-buffer smoke, plus static/skinned early/mid/late transparent CSG captures | passed; prepared extinction declares the four removed-interval aliases as exact-range `UnorderedAccess` reads. The compiler preserves four state seeds across the split depth-warp packet and waits for both it and the interval producer; direct/unprepared extinction paths retain their native bridge. |
| Graph-owned AVBOIT accumulation CSG interval-sample state follow-up: 58 task-graph, 18 ECS graphics, descriptor-buffer smoke, plus static/skinned early/mid/late transparent CSG captures | passed; prepared accumulation declares the four removed-interval aliases as exact-range `UnorderedAccess` reads. The compiler preserves four source state seeds across split depth-warp, extinction, and integration packets; direct/unprepared accumulation paths retain their native bridge. |
| Graph-owned lagged deferred bindless-selector follow-up: FrontierSafe graph unit and descriptor-buffer smoke | passed; the immutable history-selector upload is pinned to and merged into Deferred Lighting's existing Compute packet, retains the external history completion wait, and hands off `Common` to `ConstantBuffer` without extending the Lighting-to-Composite range |
| Graph-owned ray-trace material-context selector follow-up: FrontierSafe graph unit, descriptor-buffer smoke, and opaque CSG capture | passed; a post-preflight immutable selector blob merges into Shadow Preparation, publishes `Common`, and is logically handed off there to the later Compute trace consumers |
| Graph-owned caustic emission-target follow-up: FrontierSafe graph unit, descriptor-buffer smoke, and caustic-sphere capture | passed; the preflight-frozen refractive AABB payload merges into Shadow Preparation, publishes `Common`, and is logically handed off there to software and hardware caustic consumers |
| Graph-owned Surfel frame-constants follow-up: FrontierSafe graph unit, descriptor-buffer smoke, and forced-software GI capture | passed; the immutable 80-byte payload merges into Shadow Preparation, publishes `Common`, and is logically handed off there to later asynchronous Surfel GI work |
| Graph-owned shadow material-context batch follow-up: FrontierSafe graph unit, descriptor-buffer smoke, and forced-software GI capture | passed; all three immutable streams merge into Shadow Preparation, publish `Common`, and are handed off there as `ShaderResource` to later asynchronous trace consumers |
| Graph-owned software scene-BVH pair follow-up: FrontierSafe graph unit, descriptor-buffer smoke, and forced-software GI capture | passed; the frozen node and leaf-instance uploads merge into Shadow Preparation, publish `Common`, and are handed off there as one `ShaderResource` topology payload to later asynchronous trace consumers |
| Graph-owned opaque hardware TLAS build follow-up: renderer build, FrontierSafe graph unit, and descriptor-buffer smoke | passed; `nwb_ecs_render` built, graph tests passed 50/50, and descriptor smoke passed 75 tests with 10 expected topology skips. The graph unit proves the backing-buffer `Common` to `AccelStructRead` handoff and that Shadow Preparation, rather than the frozen preflight data, owns later Compute consumers. |
| Graph-owned opaque hardware BLAS build/refit follow-up: renderer build, FrontierSafe graph unit, and descriptor-buffer smoke | passed; `nwb_ecs_render` built, graph tests passed 50/50, and descriptor smoke passed 75 tests with 10 expected topology skips. The graph unit covers both a frozen BLAS build and a state-only compatibility BLAS backing, proving their `Common` to `AccelStructRead` handoff remains owned by Shadow Preparation before later Compute consumers. |
| Graph-owned software-only per-mesh SW-BVH build/refit follow-up: renderer/runtime build, FrontierSafe graph unit, descriptor-buffer smoke, and forced-software transparent capture | passed; the graph unit proves `Common` to `UnorderedAccess` ownership for parent and shared scratch state, graph tests passed 50/50, descriptor smoke passed 75 tests with 10 expected topology skips, and the forced-software transparent capture completed. |
| Graph-owned hybrid per-mesh SW-BVH follow-up: renderer build, FrontierSafe graph unit, descriptor-buffer smoke, and hardware transparent-multi capture | passed; the frozen mesh build is accepted only after its commands record, while a failed optional software tail discards that plan and still submits the valid hardware opaque result. Graph tests passed 50/50, descriptor smoke passed 77 tests with 11 expected topology skips, and the Vulkan/X11 transparent-multi capture passed. |
| Graph-owned hybrid scene-BVH pair follow-up: renderer build, FrontierSafe graph unit, descriptor-buffer smoke, and hardware transparent-multi capture | passed; the immutable node/leaf pair is accepted after the retained traversal table restores its exact mesh descriptor arrays, or after the compatibility revalidation succeeds. A mismatch discards the optional pair and preserves the valid hardware opaque result. Graph tests passed 50/50, descriptor smoke passed 77 tests with 11 expected topology skips, and the Vulkan/X11 transparent-multi capture passed. |
| Graph-owned hybrid shadow material-context follow-up: renderer build, FrontierSafe graph unit, descriptor-buffer smoke, transparent-multi, and skinned-caustic captures | passed; the final software-compatible immutable triple now survives hardware TLAS recording and is restored by the retained optional software traversal table. A tail mismatch takes direct revalidation, then clears the triple and restores the preflight-retained immutable hardware context before accepting opaque shadows; only a stale snapshot uses the direct hardware retry, and a failed restoration disables only transparent fold, caustics, and surfel GI. Graph tests passed 50/50, descriptor smoke passed 77 tests with 11 expected topology skips, and both Vulkan/X11 captures passed. |
| Graph-owned hybrid software scene-traversal follow-up: renderer build, FrontierSafe graph unit, descriptor-buffer smoke, transparent-multi, and skinned-caustic captures | passed; healthy hybrid preflight retains owning node/position/index/attribute buffers, exact descriptor slots, mesh versions, byte sizes, primitive counts, and source mutation versions. Shadow Preparation restores the matching table without CPU BVH/material regather; a plan miss preserves direct revalidation before the opaque fallback. Graph tests passed 50/50, descriptor smoke passed 77 tests with 11 expected topology skips, and both Vulkan/X11 captures passed. |
| Hybrid software traversal fallback proof: renderer build, FrontierSafe graph unit, descriptor-buffer smoke, and Vulkan/X11 transparent-multi capture | passed; a test-only one-shot fault models both the retained-table and direct-revalidation tail failing. Shadow Preparation clears the optional software pair/triple, restores the retained immutable hardware context, and accepts opaque hardware shadows; the next healthy hybrid packet reports recovery. Graph tests passed 50/50, descriptor smoke passed 77 tests with 11 expected topology skips, and the fallback capture passed. |
| Frozen hybrid hardware material-context fallback follow-up: renderer build, FrontierSafe graph unit, descriptor-buffer smoke, transparent-multi, skinned-caustic, and forced-fallback captures | passed; preflight retains the hardware material table, instance stream, typed bytes, destination identities, descriptor handles, capacities, and source mutation versions before the software-compatible upload supersedes it. A forced optional-tail miss restores that immutable payload without a recording-time renderer/material regather; stale snapshots retain the direct retry boundary. Graph tests passed 50/50, descriptor smoke passed 77 tests with 11 expected topology skips, the transparent and skinned-caustic captures passed, and the Vulkan/X11 fallback capture observed the frozen restore. |
| Stale hybrid hardware material-context fallback proof: renderer build, FrontierSafe graph unit, descriptor-buffer smoke, and Vulkan/X11 transparent-multi capture | passed; a test-only one-shot fault rejects the retained hardware snapshot after forcing the optional traversal tail to fail. Shadow Preparation takes the established direct hardware retry, does not emit the frozen-restore path, then the next healthy packet recovers. Graph tests passed 50/50, descriptor smoke passed 77 tests with 11 expected topology skips, and the stale fallback capture passed. |
| Hybrid fallback target-scene paired A/B: renderer build, Vulkan/X11 stress capture, and GPU timing | passed on AMD BC-250 (RADV GFX1013) without validation-layer timing overhead. The paired fixed-yaw ten-character animated scene produced 26 `render.frame` intervals per arm: healthy hybrid median 18.2199 ms, persistent opaque-HW fallback median 9.0301 ms (-50.438%). `render.shadow_visibility` fell from 12.0338 to 2.8551 ms while opaque tracing remained within -0.738% (1.6194 vs 1.6075 ms); healthy `render.shadow_transparent_trace` was 7.6115 ms and intentionally absent from the fallback arm. The fallback observed the forced traversal miss, transparent-shadow absence, and frozen immutable hardware-context restore with no forbidden runtime/validation log. The reproducible `hybrid-shadow-boundary` workflow retains logs, captures, and JSON/Markdown timing evidence under `.cozter/out/ab-results/`. |
| Graph-owned hybrid TLAS build follow-up: renderer build, FrontierSafe graph unit, descriptor-buffer smoke, transparent-multi, and skinned-caustic captures | passed; healthy hybrid preflight retains the exact instance descriptors, BLAS handles, selected TLAS, and backing generation in Shadow Preparation. A capture or record mismatch clears only that plan and retries the direct TLAS build while retaining the graph-owned software material context. Graph tests passed 50/50, descriptor smoke passed 77 tests with 11 expected topology skips, and both Vulkan/X11 captures passed. |
| Graph-owned hybrid hardware-BLAS follow-up: renderer build, FrontierSafe graph unit, descriptor-buffer smoke, transparent-multi, and skinned-caustic captures | passed; independent frozen hardware mesh builds now record in Shadow Preparation for hybrid frames, while a mismatch discards the plan and retries the direct compatibility loop instead of rejecting the opaque-shadow fallback. Graph tests passed 50/50, descriptor smoke passed 77 tests with 11 expected topology skips, and both Vulkan/X11 captures passed. |
| Dedicated-Transfer recovery-frontier follow-up: graph unit and descriptor-buffer smoke | passed; graph tests passed 50/50 and descriptor smoke passed 77 tests with 11 expected topology skips. The new native test accepts a graph-owned Transfer upload, injects a rejected dependent Graphics suffix, verifies the transaction emits its exact physical Transfer token, then confirms the Device receives that token on the independent recovery-tail submission. This adapter skips only the dedicated-Transfer execution because it has no Transfer-only family. |
| Graph-owned frame GPU-timing reset: graph unit, ECS graphics, and descriptor-buffer smoke | passed; the reset now compiles as one strict primary-Graphics graph packet, and only its accepted task callback publishes timer-query availability while failed work remains revoked. Graph tests passed 50/50, ECS graphics passed 18/18, and descriptor smoke passed 78 tests with 11 expected topology skips. The targeted rejection/retry smoke proves a rejected reset leaves no stale dynamic-rendering query availability and the following accepted graph preamble recovers it. |
| Graph-owned CSG interval rect-clear follow-up: renderer build, graph/ECS/descriptor tests, and Vulkan/X11 CSG captures | passed; opaque G-buffer and prepared-transparent AVBOIT now run an exact two-target frozen-rect clear task before their native CSG producers, with graph-declared `CopyDest` to `UnorderedAccess` handoff over all peel ID layers and the single receiver-event-count layer. Graph tests passed 51/51, ECS graphics passed 18/18, descriptor smoke passed 78 tests with 11 expected topology skips, and opaque plus transparent CSG early/mid/late captures passed. The direct CSG clear remains the compatibility fallback for unprepared transparent work. |
| Graph-owned CSG clip-buffer entry-state follow-up: renderer build, graph/ECS/descriptor tests, and Vulkan/X11 CSG captures | passed; opaque and prepared-transparent CSG callbacks now consume graph-declared receiver/cutter `ShaderResource` and clip-context/interval-sample `ConstantBuffer` states without restating the native heap-buffer bridge. Graph tests passed 60/60, ECS graphics passed 18/18, descriptor smoke passed 83 tests with 11 expected topology skips, and opaque plus early/mid/late transparent CSG captures passed. Direct, unprepared, and empty gathered-work paths retain the native helper; intra-task CSG UAV fences remain native. |

The latest local command-IR evidence is under
`.cozter/out/ab-results/command-ir/20260811_050635/`. It is intentionally local/ignored A/B evidence rather than a
checked-in performance claim. The profile is a CPU probe for `CopyBuffer` only; it does not justify persistent
runtime IR templates or general direct-Vulkan IR replay.

## Open criteria preventing strict final acceptance

These are substantive scope gaps, not failures hidden by the hardware waiver.

1. **End-to-end physical queue identity (partially addressed).** Vulkan now assigns every accepted
   `QueueSubmissionToken` a concrete queue index and device generation. Graph recording, packet acceptance, and
   imported completion bindings require that identity; the renderer and native smoke topologies obtain it from the
   Device; and a token from a retired device is rejected at the native submission boundary. An opt-in second
   Graphics queue from the same Vulkan family is now registered, explicitly selected graph packets route to it
   deterministically, and same-family state handoffs use exact physical timeline waits without a spurious ownership
   barrier. There is still no policy for same-class queues from different families, dynamic queue scaling, or target
   scene performance evidence for the new route.

2. **Complete graph ownership.** Renderer code still builds packet ranges, controls recording/submission, and holds
   legacy state-handoff data. The shared deferred path now owns per-frame ImGui draw/font/texture uploads; its
   direct path remains only as a compatibility fallback for worlds without a graph-owning renderer or a rejected
   graph attempt. Frame GPU-timing query-pool reset is now a strict primary-Graphics graph packet whose accepted
   callback alone publishes query availability; failed work remains revoked. Public buffer/texture setup uploads, decoded texture-asset uploads, and the shared deferred
   mesh-view, scene-light, scene-shading, runtime skinning selector/palette/copy/compute updates, and opaque material instance/typed updates
   now use the graph-owned primitive path. The transparent AVBOIT interval producer, occupancy, extinction, and
   accumulation phases now freeze and publish their own per-write-point material/CSG streams, preserving the shared
   interval sample state across the low-resolution raster passes. Current and lagged deferred bindless selectors,
   the ray-trace material-context selector, normal Shadow Visibility and Software Caustics descriptor-visible entry states, caustic
   emission-target stream, surfel-frame constants, hardware-only,
   forced-software, and healthy hybrid shadow material-context batches plus their retained immutable hardware
   fallback context, software-only and hybrid scene-BVH pairs
   and healthy hybrid software traversal tables, software-only and hybrid per-mesh SW-BVH build/refit,
   opaque and healthy hybrid hardware TLAS, and opaque and hybrid hardware BLAS build/refit transactions are acceptance-safe
   graph-owned preparation work; other specialized descriptor/resource updates still retain direct native recording
   or submission. The graph
   therefore does not yet authoritatively own all frame work and state retirement.

3. **Packet scheduling and recording completion (partially addressed).** Current merging is limited to compatible
   immediate predecessors. The compiler-derived native ready-frontier recorder is implemented for explicit opt-in
   packets, with isolated per-packet state scratch and independent native command buffers/pools; all other packets,
   command-IR capture, and legacy external-state overrides retain serial recording. Frontier splitting/scoring and
   reusable per-worker graph command-arena leases are still not implemented.

4. **Generic recovery and invalidation proof.** The accepted-token transaction is sound for the exercised paths,
   stale imported completion tokens have graph and native-submission rejection coverage, and recording/transaction
   state is now invalidated and recreated across a compiled-graph generation change. A real headless Graphics
   device-lifetime test now verifies the renderer-style reset before teardown, rejects the retired token generation,
   and records/submits a fresh packet after recreation. The transaction unit path covers accepted Compute and
   Transfer producers, and a dedicated-Transfer headless test now accepts a graph-owned upload, rejects its later
   Graphics suffix, verifies the exact physical Transfer frontier token, and confirms that the Device receives it
   on an independent late Graphics recovery submission. The qualified-hardware execution remains topology-gated:
   this adapter correctly skips it because it has no dedicated Transfer-only family.

5. **Final parity and performance evidence.** There is no immutable current baseline, legacy-to-graph pixel parity
   corpus, or complete bindless-domain audit. The paired target-scene critical-path comparison now covers the retained
   hybrid HW-to-SW transparent-shadow safety boundary, but it intentionally measures a quality-degrading fallback and
   does not replace general legacy-to-graph parity evidence. Dedicated-Transfer ownership/bandwidth evidence and an
   external profiler trace remain deferred because the available adapter lacks that queue family.

## Required decision for a strict closeout

Do not relabel this result as final architectural completion without either implementing the five areas above or
explicitly waiving them in a revised, version-controlled acceptance scope. The next implementation work should
finish the remaining graph-owned asset/resource-update paths and specialized descriptor/resource updates, then
collect the dedicated-Transfer evidence still called out above.
