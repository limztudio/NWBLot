# Transfer-queue upload profile

This target-hardware A/B harness provides Phase 10 setup-upload route evidence. It is a focused microprobe, not the
graph-wide Phase 10 acceptance gate. It compares the same repeated large upload with:

- an explicit `Graphics` producer baseline while keeping the same dedicated Transfer queue topology; and
- the public automatic route, which must select a genuinely dedicated `Transfer` producer.

Each iteration first submits independent Graphics buffer copies, creating a repeatable contention window for a GPU
profiler. It then immediately submits a Graphics consumer copy of the uploaded resource without a CPU wait. The
consumer's timeline token observes one actual Graphics readiness bridge per automatic upload. The automatic route
uses concurrent Graphics/Transfer resource sharing, not an exclusive queue-family ownership transfer; the zero
ownership figure is therefore an expected setup contract, not a global barrier counter.

The JSON distinguishes logical payload bytes, an asynchronous-ingress copy count (zero for this synchronous probe),
the Graphics readiness-copy traffic, and modeled read-plus-write copy traffic. It keeps only two upload windows in
flight by default, avoiding a large-allocation/eviction benchmark. Those modeled values describe the command workload
only; use the external trace for actual memory-controller bandwidth, cache behavior, and copy-engine overlap.

From the repository root:

```bash
python launcher.py transfer-queue
```

The default workload uses 12 uploads of 16 MiB each and 16 Graphics copies of a 32 MiB buffer per iteration. It
writes a timestamped bundle under `.cozter/out/ab-results/transfer-queue/`, including both process logs,
JSON/Markdown results, host `vulkaninfo` inventory when available, and exact direct commands for an external capture.
The runner pins enumeration index `0` by default; use `--adapter-index N` to select another adapter. Both native
payloads record and compare the selected adapter's vendor ID, device ID, and UUID. Host inventory is informative and
does not by itself identify the selected device.

The harness returns `77` when the adapter has no distinct Transfer-only family or GPU validation is unavailable. That
is an environment skip, not a fallback result: the automatic code route must be profiled on actual dedicated-Transfer
hardware. The host completion values are only CPU-visible envelope measurements; do not interpret their MiB/s as
GPU-copy bandwidth. Use `--no-gpu-validation` for a final-build target that cannot enable the validation runtime.

Run the texture variant with:

```bash
python launcher.py transfer-queue -- --resource texture
```

Capture each command from `capture-commands.txt` using the target GPU profiler, then attach its trace/report while rerunning the workflow:

```bash
python launcher.py transfer-queue --external-profiler-report /path/to/capture.rgp
```

Without an external trace the runner returns `2` with `incomplete_external_capture_required`, after preserving the
route/correctness artifact. A supplied trace changes the result to `capture_attached_pending_review`, which also
returns `2`: a human or lab analysis must still assess bandwidth, copy-engine overlap, idle gaps, and representative
graph packet/ownership telemetry before Phase 10 is accepted. Use `--require-external-profiler` after `--` when an
automated lab job should treat the missing trace as a hard failure.
