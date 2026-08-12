# Hybrid transparent-shadow fallback boundary

This target-hardware A/B workflow measures the retained hybrid HW -> SW transparent-shadow
fallback boundary on the animated ten-character stress scene. Both arms freeze the same yaw and
render the same opaque, transparent, skinned, and two-light setup:

- `nwb_hybrid_shadow_boundary_healthy_benchmark` records the healthy hybrid transparent-shadow tail.
- `nwb_hybrid_shadow_boundary_fallback_benchmark` forces that optional traversal tail to miss every frame,
  restores the captured immutable hardware material context, and retains opaque hardware shadows.

The fallback intentionally removes transparent shadows. Its capture is therefore an inspection
artifact, not a visual-parity input. The runner instead verifies the one-time forced-miss, absence,
and frozen-context-restore log markers, checks for Vulkan/runtime failures, and compares GPU
timestamp medians from the same scene.

From the repository root:

```bash
python launcher.py hybrid-shadow-boundary
```

The command configures and builds the paired benchmarks, then writes a timestamped artifact bundle
under `.cozter/out/ab-results/hybrid-shadow-boundary/`. It measures without Vulkan validation by
default so `--gpudbg` layer work does not dominate the timing evidence. Run the correctness-oriented
variant explicitly when needed:

```bash
python launcher.py hybrid-shadow-boundary --gpu-validation
```

Forward runner options after `--`:

```bash
python launcher.py hybrid-shadow-boundary -- --measure-seconds 30 --minimum-samples 12
```

The default success condition requires six `render.frame` intervals from each arm, expected arm
markers, clean runtime/validation logs, and nonblank native-window captures. It records the fallback
frame delta but does not classify it as good or bad by default because the fallback deliberately
changes transparent-shadow quality. If a target has an agreed budget, make it explicit:

```bash
python launcher.py hybrid-shadow-boundary -- \
  --maximum-fallback-frame-regression-percent 3.0
```

The report compares `render.frame` plus any published shadow/tail scopes. A missing optional scope
means that arm did not publish it; it is never silently converted into zero cost. Artifacts include
per-arm timing files, complete logs, BMP captures, and `hybrid_shadow_boundary_report.json` / `.md`.
Run on a target GPU with a visible native X11/Xwayland or Windows window; this is a hardware evidence
workflow, not a headless CTest.
