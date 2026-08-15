# Graph-owned soft-transparent shadow-fold A/B

This target-hardware workflow measures the graph-owned split between opaque soft-shadow visibility and
the terminal transparent fold against the retained monolithic callback. Both arms render the same frozen-yaw,
ten-character animated hybrid-shadow stress scene and preserve the same visible shadow work:

- `nwb_soft_transparent_shadow_fold_graph_benchmark` enables the normal graph-owned fold;
- `nwb_soft_transparent_shadow_fold_monolithic_benchmark` uses a test-only switch to retain the compatibility
  callback.

The runner verifies each arm's renderer-path marker, rejects runtime and Vulkan-validation errors, captures both
native windows for inspection, and compares per-pass GPU timestamp medians. Captures are diagnostic rather than a
byte-exact gate because temporal soft shadows are intentionally sampled over time.

From the repository root:

```bash
python launcher.py soft-transparent-shadow-fold
```

The default timing run is deliberately without validation-layer overhead. Run correctness-oriented collection with:

```bash
python launcher.py soft-transparent-shadow-fold --gpu-validation
```

Forward timing controls after `--`:

```bash
python launcher.py soft-transparent-shadow-fold -- --measure-seconds 30 --minimum-samples 12
```

Artifacts are stored under `.cozter/out/ab-results/soft-transparent-shadow-fold/`, including both BMP captures,
logs, timestamp files, and JSON/Markdown reports. An agreed graph-route frame budget can be enforced explicitly:

```bash
python launcher.py soft-transparent-shadow-fold -- \
  --maximum-graph-frame-regression-percent 3.0
```
