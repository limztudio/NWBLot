# Command-IR copy-buffer overhead profile

This Phase 11 workflow measures the optional command-IR path after native recording is stable. One native process
records the same built-in buffer-copy workload through the direct path and through command-IR capture, then measures
reader decode, replay preflight, `Core::CommandList` replay, and the experimental direct-Vulkan `CopyBuffer`
lowerer. It is a CPU-overhead probe, not a GPU benchmark or a production adoption gate. Texture-copy and clear
record shapes need separately labelled future corpora; do not use this copy-buffer result as an aggregate all-opcode
cost.

From the repository root:

```bash
python launcher.py command-ir
```

The default uses 4,096 copy-buffer records, 3 warm-up samples, and 11 measured samples per stage. `--records` accepts 1 through
65,536, `--samples` accepts 1 through 64, and warm-up counts must be positive. To increase the sample size or choose
a different adapter:

```bash
python launcher.py command-ir --records 8192 --warmup 5 --samples 21 --adapter-index 1
```

The native and capture recording arms alternate order across measured samples to reduce fixed cache/pool ordering
bias; reader, preflight, and replay are then measured from that sample's captured stream.

Vulkan validation is on by default. Use `--no-gpu-validation` only for a target that cannot expose the validation
layer. The workflow writes a timestamped artifact bundle under `.cozter/out/ab-results/command-ir/` containing:

- `command_ir_profile.log`, the complete native process output;
- `profile-command.txt`, the exact native invocation;
- `command_ir_profile_report.json` and `.md`, including all raw CPU nanoseconds-per-command samples and summaries;
- `vulkaninfo-summary.txt` when the host utility is available.

The runner requires an `ok` payload to prove the requested record/sample parameters, pinned adapter identity, valid
stream, expected-output replay checksum, exact decoded/preflight/replayed record counts, zero logger errors, and zero capture
allocation/reallocation deltas during measured capture. It returns `77` when the native profile explicitly reports a
Vulkan/profile environment skip; all other failed invariants return `1` after preserving the artifacts.

`native_record` and `capture_record` are full-path comparative timings: both include native packet recording and its
backend command-list creation. Both replay timings exclude command-list creation/open/close but include stream
preflight and lowering. The direct-Vulkan timing also excludes its required graph-owned state setup, which the
workflow performs and validates separately; the prototype emits only `vkCmdCopyBuffer` and never replaces compiler
barriers, state seeds, or packet submission. Only the dedicated capture arena has an allocation-free gate; none of
these CPU numbers measure GPU execution time.

## Persistent-template decision

Do not add persistent command-IR templates to runtime yet. The direct-Vulkan prototype is intentionally tooling-only,
supports only `CopyBuffer`, and relies on the graph recorder to establish resource states before lowering. It neither
eliminates capture/validation cost nor provides representative evidence for renderer passes with bindings, draws,
dispatches, textures, or clears.

Re-evaluate templates only after a representative workload shows an end-to-end benefit and the implementation has a
safe invalidation contract for graph generation, resource IDs, pipeline IDs, descriptor state, and packet barriers.

Run either layer's no-Vulkan verification with:

```bash
python launcher.py command-ir --self-test
python tests/ab/command_ir/run.py --self-test
```
