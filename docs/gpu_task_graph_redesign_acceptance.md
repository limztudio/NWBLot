# GPU task-graph redesign acceptance audit

**Decision:** conditional closeout; **not** strict final architecture acceptance.

**Reviewed revision:** `c62605d9` (`Fix acceptance policy regressions`), 2026-08-11.

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

The latest local command-IR evidence is under
`.cozter/out/ab-results/command-ir/20260811_050635/`. It is intentionally local/ignored A/B evidence rather than a
checked-in performance claim. The profile is a CPU probe for `CopyBuffer` only; it does not justify persistent
runtime IR templates or general direct-Vulkan IR replay.

## Open criteria preventing strict final acceptance

These are substantive scope gaps, not failures hidden by the hardware waiver.

1. **End-to-end physical queue identity.** `GpuPhysicalQueueId` is compiler metadata, but submission tokens and
   state handoffs ultimately identify `CommandQueue` classes. The runtime does not yet support multiple same-class
   physical queues or carry device generation through every external completion identity.

2. **Complete graph ownership.** Renderer code still builds packet ranges, controls recording/submission, and holds
   legacy state-handoff data. Setup uploads, asset uploads, skinning, and UI also retain direct native recording or
   submission paths. The graph therefore does not yet authoritatively own all frame work and state retirement.

3. **Packet scheduling and recording completion.** Current merging is limited to compatible immediate
   predecessors. Frontier splitting/scoring, graph-level parallel packet recording, and per-worker graph command
   arenas from the proposal are not implemented.

4. **Generic recovery and invalidation proof.** The accepted-token transaction is sound for the exercised paths, but
   recovery is not proven to join every possible Transfer queue route, and there is no end-to-end stale
   compiled-graph/device-recreation lease test.

5. **Final parity and performance evidence.** There is no immutable current baseline, legacy-to-graph pixel parity
   corpus, complete bindless-domain audit, or target-scene critical-path comparison. Dedicated-Transfer ownership/
   bandwidth evidence and an external profiler trace remain deferred because the available adapter lacks that queue
   family.

## Required decision for a strict closeout

Do not relabel this result as final architectural completion without either implementing the five areas above or
explicitly waiving them in a revised, version-controlled acceptance scope. The next implementation work should start
with a scoped design decision for end-to-end physical queue identity and graph-owned upload/recovery paths; those
choices determine the correct shape of packet frontiers, command arenas, and final performance validation.
