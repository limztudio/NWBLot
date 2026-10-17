# Allocation ownership in perf telemetry

Enable the existing perf capture on `NWB::Core::Frame`:

```cpp
frame.setTelemetryCapture(NWB::Core::Telemetry::CaptureOptions::PerfOnly());
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

GlobalArena and heap backing byte counters use the allocator's usable allocation size consistently, including reallocations. ScratchArena records its aligned suballocations, and PersistentArena records its managed native block spans. These are allocation-accounting figures, not process resident-memory measurements.

`heapBacking` is the inclusive CoreAlloc backing total, including arena pool/chunk allocations and global new/delete. Its counters are collected per thread and combined when sampled. `peakBasis: sampledHeap` is the largest aggregate usage observed by those samples; transient peaks between samples can be higher. Heap backing must not be added to arena usage. `explicitScope` preserves manually recorded snapshots, which may alias an automatically collected arena. The JSON `perf.memorySources` summaries keep all three domains separate; memory summaries are reported only within their source domain.

For in-process inspection, enable `NWB::Core::Perf::CaptureOptions::memory`, publish the frame, and use the source-aware view:

```cpp
const auto& snapshot = session.memoryView().snapshot(ownerName, NWB::Core::Perf::MemorySource::Arena);
const auto& delta = session.memoryView().delta(ownerName, NWB::Core::Perf::MemorySource::Arena);
```

The registry stores owner identities once. Allocations update the arena's own counters and a heap counter shard owned by the current thread. Capture-time aggregation and arena registration/retirement handle owner totals; allocation operations do not contend on shared owner totals. There is no text formatting, stack walking, telemetry encoding, or registry lookup per allocation after thread-shard initialization. Counters continue while capture is disabled so later frees and capture re-enablement remain accurate. Disabled memory capture skips owner aggregation and snapshot publication. Concurrent snapshots are samples of atomic counters, not a stop-the-world transaction; exact totals settle after producers finish.

For readable names in `opt` and `fin`, generate the existing `.namesym` sidecars from a matching build and workload. For this Windows ARM64 preset, the existing target is:

```powershell
python -m launcher build nwb_namesym --configure-preset windows-clang-arm64 --config opt
```

That target runs the project's established symbol-collection workloads and bundles sidecars beside the matching logserver. Start or restart the logserver after generating those sidecars so it loads them before producing reports. Build-mode symbol export now includes retained arena owner names, including arenas created before symbol callbacks were installed or destroyed before export. This collection does not require perf capture to be enabled. The logger resolves a raw hash display name against its loaded symbols using the full binary identity; explicitly supplied display labels are preserved. Without a matching symbol entry, the stable hash remains available for correlation.

This collection identifies named allocation owners and their lifetime totals. It does not capture individual allocation addresses or call stacks. Name-symbol resolution stays outside allocator paths. The current memory payload uses version 1 with a 192-byte header. Readers accept only this version.

Telemetry payload parsers return `Expected<Payload>` from their existing telemetry arena. Frame-graph parsing also borrows the caller's operation scratch for temporary statistics validation, and frame-graph encoding borrows that same scratch while retaining the caller-owned reusable destination.

The frame-graph registry retains graph-buffer capacity in its owning arena between captures, clears borrowed labels and descriptors before every capture ends (including unwind), and rejects nested capture of the same registry. Builder identity maps, sorting, string tables, pending-name lookup, and packet validation share one scratch arena supplied by the owning operation; contributors borrow it through the joined builder. Persistent caller-owned descriptor buffers and parsed/recorded payload ownership retain their existing lifetimes.

Other telemetry payload parsers use their existing telemetry arena, and `BuildTelemetryReport(arena, events, scratchArena)` returns `Expected<TelemetryReport>`. Check each result before reading or moving it. `DecodeEvent` returns one `DecodedEvent` containing the event and consumed byte count; `DecodeEventStream` returns the consumed count while retaining the caller-owned `Recorder`. Failures retain decoder status and offset. Existing payload encoders keep reusable destination capacity. See [produced values and expected failures](expected_results.md).

Global diagnostic observation through `DiagnosticCaptureGuard` uses bounded storage so cleanup and unwind diagnostics can be captured without allocating or waiting for the recorder mutex. Enabling the diagnostic capture flag prepares 64 dedicated slots with 8192 bytes of encoded payload capacity each, including the 40-byte diagnostic header. This reserves 512 KiB of payload capacity per recorder, plus slot objects, event headers, allocator overhead, and space for at least 64 additional owning pointers in the recorder's ordinary event vector. Preparation is an ordinary fallible allocation operation in `setCaptureOptions`; it completes before diagnostic observation is enabled. Repeating the same options or constructing the per-frame capture scope reuses that preparation.

Each observed diagnostic is encoded into an 8192-byte stack buffer, then copied into one prepared slot. The observer takes the recorder mutex only with `try_lock` and publishes only while both a dedicated slot and existing event-vector capacity are available. Ordinary record builders use separate slots and retain their existing allocation and reentry behavior. Successfully captured diagnostics share the normal event ordering and `EventView` lifetime. Oversized diagnostics are rejected whole, with no truncation. Direct `RecordDiagnostic` calls retain their dynamic payload API and are not subject to the observer's payload or slot bounds.

`Recorder::droppedDiagnosticCount()` reports a cumulative recorder-lifetime count of observer events rejected for oversized payloads, mutex contention, exhausted dedicated slots, or exhausted publication capacity. Disabled or filtered diagnostics do not increase it. Drops do not log another diagnostic. `clear()` recycles the prepared slots without resetting the counter. Turning off only the diagnostic flag retains its prepared pool and already-published records while other capture remains enabled; enabling the flag again does not enlarge the pool. Disabling all capture destroys the retained records and prepared slots, while preserving the cumulative drop count. The next diagnostic enablement prepares one fresh pool. The usual quiescent readback rule still applies when inspecting an `EventView` while another thread might clear or disable its recorder.

`TextLogCaptureLogger::droppedTextLogCount()` reports cumulative text-log payload failures without recursively logging them. Disabled or filtered text events do not count as drops, including capture-option changes while a normal recorder builder runs. Forwarding to the configured logger still occurs for rejected or filtered capture. Public `RecordTextLog` retains its boolean publication result; the capture logger uses a void command with an internal recorded/filtered/failed outcome. Direct text-payload building takes caller scratch to preserve aliased input; ordinary recorder logging uses its exclusive destination lease and retains the warm path without transient alias-copy allocation.

The allocator and telemetry regression suites exercise reallocation/failure accounting, bulk arena retirement, concurrent owners, source identity isolation, capture toggles, payload validation, and JSON export.
