# GPU task-graph redesign acceptance audit

**Decision:** conditional closeout; **not** strict final architecture acceptance.

**Reviewed baseline:** `c62605d9` (`Fix acceptance policy regressions`), 2026-08-11.

**Follow-up implementation:** physical queue identity, frontier-safe deferred scheduling, graph-bound presentation,
graph-owned per-frame ImGui uploads, opt-in ready-frontier native recording, and opt-in same-class physical Graphics
routing, actual-device stale packet-recording recreation coverage, and graph-owned public setup uploads,
graph-owned deferred mesh-view, scene-light, and scene-shading frame updates, and graph-owned decoded texture-asset
uploads, graph-owned runtime-skinning joint-palette uploads, graph-owned opaque material draw-stream uploads, and
graph-owned opaque CSG receiver/cutter streams,
2026-08-11.

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
- Active runtime skinning joint palettes are now derived before recording, copied into immutable graph upload blobs,
  compiled and submitted on the primary Graphics transport, then exposed through the terminal native state handoff
  to the established skinning compute list. The compute dispatch and its one-time bindless selector write retain
  their acceptance-guarded compatibility path pending the specialized descriptor-update tranche.
- In the shared deferred path, ImGui vertex/index payloads and requested font/texture updates are retained as
  immutable graph blobs, lowered through the built-in upload tasks, and made dependencies of the terminal overlay
  or upload-completion packet. Small deltas stay on Graphics; amortizable updates prefer Transfer.
- The shared deferred graphics prefix resolves changed mesh-view, scene-light, and scene-shading data before graph
  compilation. Each payload is retained as an immutable graph blob, uploaded through a Graphics-routed built-in
  buffer task, and committed to its CPU mirror only when the matching packet is accepted. These automatic-state
  buffers return to `Common` at packet close; their first declared consumer owns the transition to
  `ConstantBuffer` or `ShaderResource`.
- The opaque G-buffer now freezes its material draw ordering and CSG CPU frame payload during graph declaration.
  Its instance and typed-material bytes are retained as immutable graph blobs and uploaded through Graphics-routed
  built-in buffer tasks after deferred clear. The tasks publish the buffers' automatic `Common` close boundary; the
  G-buffer declares the transient `ShaderResource` reads. CSG receiver-range and cutter records from that same
  frozen payload are also immutable graph blobs, uploaded in order before the G-buffer, and explicitly declared as
  transient `ShaderResource` reads. The target-generation clip-context slot payload and interval sample state remain
  narrow direct compatibility paths because they are specialized descriptor/resource updates.
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
| Graph-owned skinning joint-palette follow-up: `nwb_ecs_mesh_skinning`, `nwb_ecs_graphics_tests`, graph/descriptor smoke, and runtime skinning smoke | passed; the graph packet publishes `ShaderResource` joint palettes to the primary-Graphics skinning compute list; 17/17 ECS unit tests, 45/45 graph tests, descriptor smoke 74 passed with 10 expected topology skips, and the opt-in one-character fast smoke completed its animated case |
| Graph-owned opaque material-stream follow-up: `nwb_ecs_graphics_tests`, graph/descriptor smoke, and runtime skinning smoke | passed; 17/17 ECS unit tests, 45/45 graph tests, descriptor smoke 74 passed with 10 expected topology skips, and the one-character Vulkan smoke completed its animated opaque-material case |
| Graph-owned opaque CSG receiver/cutter-stream follow-up: static and compute-emulation CSG capture smoke | 2/2 passed; both Vulkan/X11 scenes rendered opaque CSG receivers through the ordered graph tasks before Opaque G-Buffer |

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
   graph attempt. Public buffer/texture setup uploads, decoded texture-asset uploads, and the shared deferred
   mesh-view, scene-light, scene-shading, runtime skinning joint-palette, and opaque material instance/typed updates
   now use the graph-owned primitive path. Skinning compute dispatch/its selector update, transparent material
   streams, CSG clip-context/interval-sample state, and other specialized descriptor/resource updates still retain
   direct native recording or
   submission. The graph therefore does not yet authoritatively own all frame work and state retirement.

3. **Packet scheduling and recording completion (partially addressed).** Current merging is limited to compatible
   immediate predecessors. The compiler-derived native ready-frontier recorder is implemented for explicit opt-in
   packets, with isolated per-packet state scratch and independent native command buffers/pools; all other packets,
   command-IR capture, and legacy external-state overrides retain serial recording. Frontier splitting/scoring and
   reusable per-worker graph command-arena leases are still not implemented.

4. **Generic recovery and invalidation proof.** The accepted-token transaction is sound for the exercised paths,
   stale imported completion tokens have graph and native-submission rejection coverage, and recording/transaction
   state is now invalidated and recreated across a compiled-graph generation change. A real headless Graphics
   device-lifetime test now verifies the renderer-style reset before teardown, rejects the retired token generation,
   and records/submits a fresh packet after recreation. Recovery is still not proven to join every possible Transfer
   queue route.

5. **Final parity and performance evidence.** There is no immutable current baseline, legacy-to-graph pixel parity
   corpus, complete bindless-domain audit, or target-scene critical-path comparison. Dedicated-Transfer ownership/
   bandwidth evidence and an external profiler trace remain deferred because the available adapter lacks that queue
   family.

## Required decision for a strict closeout

Do not relabel this result as final architectural completion without either implementing the five areas above or
explicitly waiving them in a revised, version-controlled acceptance scope. The next implementation work should
finish the remaining graph-owned asset/resource-update paths (notably transparent material streams and CSG
clip-context/interval-sample state), specialized descriptor/resource updates, and recovery proof.
