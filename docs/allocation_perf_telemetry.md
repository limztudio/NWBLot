# Allocation ownership in perf telemetry

Enable the existing perf capture on `Core::Frame`:

```cpp
frame.setTelemetryCapture(Core::Telemetry::CaptureOptions::PerfOnly());
```

`Frame` publishes named allocation owners automatically at the end of each successful frame. The existing telemetry upload flow stores the binary stream and the logger creates its JSON report under the configured telemetry report directory. Memory data is in `perf.memoryRecords`; timing CSV output remains timing data.

Each memory record contains:

- `source`: `arena`, `heapBacking`, or `explicitScope`.
- `scope`: the arena/owner name through the existing Name symbol resolver. When symbols are unavailable in opt/fin, this is the hash text.
- `identity`: the full stable binary Name identity encoded as hexadecimal, independent of symbol availability.
- `streamId` and `frameIndex`.
- `reservedBytes`, `usedBytes`, `peakUsedBytes`, and allocation/reallocation/deallocation counts.
- `peakBasis`: `largestArena`, `sampledHeap`, or `scope`, describing the peak measurement.
- `delta`: changes since that scope's preceding captured frame, or `null` for its first sample.

`arena` records aggregate arena instances with the same canonical Name. Arena names should identify a stable subsystem or operation. Their records remain for the process lifetime, so temporary scratch arenas that allocate and disappear between frame publications still contribute allocation/free counts and their individual high-water marks. `peakBasis: largestArena` means the largest individual arena peak among all instances of that owner; it is not a simultaneous sum of several arenas' peaks. Scratch and persistent arena destruction retires bulk storage; destroying a GlobalArena does not free its outstanding allocations, which remain visible.

GlobalArena and heap backing byte counters use the allocator's usable allocation size consistently, including reallocations. ScratchArena records its aligned suballocations, and PersistentArena records TLSF block sizes. These are allocation-accounting figures, not process resident-memory measurements.

`heapBacking` is the inclusive CoreAlloc backing total, including arena pool/chunk allocations and global new/delete. Its counters are collected per thread and combined when sampled. `peakBasis: sampledHeap` is the largest aggregate usage observed by those samples; transient peaks between samples can be higher. Heap backing must not be added to arena usage. `explicitScope` preserves manually recorded snapshots, which may alias an automatically collected arena. The JSON `perf.memorySources` summaries keep all three domains separate; memory summaries are reported only within their source domain.

For in-process inspection, enable `Perf::CaptureOptions::memory`, publish the frame, and use the source-aware view:

```cpp
const auto& snapshot = session.memoryView().snapshot(ownerName, Core::Perf::MemorySource::Arena);
const auto& delta = session.memoryView().delta(ownerName, Core::Perf::MemorySource::Arena);
```

The registry stores owner identities once. Allocations update the arena's own counters and a heap counter shard owned by the current thread. Capture-time aggregation and arena registration/retirement handle owner totals; allocation operations do not contend on shared owner totals. There is no text formatting, stack walking, telemetry encoding, or registry lookup per allocation after thread-shard initialization. Counters continue while capture is disabled so later frees and capture re-enablement remain accurate. Disabled memory capture skips owner aggregation and snapshot publication. Concurrent snapshots are samples of atomic counters, not a stop-the-world transaction; exact totals settle after producers finish.

For readable names in `opt` and `fin`, generate the existing `.namesym` sidecars from a matching build and workload. For this Windows ARM64 preset, the existing target is:

```powershell
cmake --build --preset windows-clang-arm64-opt --target nwb_namesym
```

That target runs the project's established symbol-collection workloads and bundles sidecars beside the matching logserver. Start or restart the logserver after generating those sidecars so it loads them before producing reports. Build-mode symbol export now includes retained arena owner names, including arenas created before symbol callbacks were installed or destroyed before export. This collection does not require perf capture to be enabled. The logger resolves a raw hash display name against its loaded symbols using the full binary identity; explicitly supplied display labels are preserved. Without a matching symbol entry, the stable hash remains available for correlation.

This collection identifies named allocation owners and their lifetime totals. It does not capture individual allocation addresses or call stacks. Name-symbol resolution stays outside allocator paths. The current memory payload uses version 1 with a 192-byte header. Readers accept only this version.

The allocator and telemetry regression suites exercise reallocation/failure accounting, bulk arena retirement, concurrent owners, source identity isolation, capture toggles, payload validation, and JSON export.
