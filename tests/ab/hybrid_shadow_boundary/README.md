# Hybrid versus opaque shadow boundary

This target-hardware A/B workflow measures the natural hybrid HW+SW shadow route against an opaque hardware-only
baseline on the animated ten-character stress scene. Both arms freeze the same yaw and use the same binary:

- `nwb_hybrid_shadow_boundary_healthy_benchmark` records the healthy hybrid transparent-shadow tail.
- The second launch sets `NWB_HYBRID_SHADOW_BOUNDARY_OPAQUE_BASELINE=1`, making every test-scene character opaque
  so the renderer naturally selects hardware shadows without a behavior-changing production hook.

The baseline intentionally removes transparent materials. Its capture is therefore an inspection artifact, not a
visual-parity input. The runner verifies RayQuery and natural route markers, checks for Vulkan/runtime failures, and
compares GPU timestamp medians without manufacturing a renderer failure.

From the repository root:

```bash
python launcher.py hybrid-shadow-boundary
```

Use `--config dbg` for correctness investigation or `--config opt` for timing evidence. The canonical launcher
rejects `fin` because Final builds omit the warning diagnostics needed to detect transient hybrid-route degradation.
For an optimized binary whose timing names are hashed, forward its generated `.namesym` sidecar with
`--healthy-namesym`; because both arms reuse the same executable, the runner applies that sidecar to both arms when
`--baseline-namesym` is omitted.

The command configures and builds the shared benchmark, then writes a timestamped artifact bundle
under `.cozter/out/ab-results/hybrid-shadow-boundary/`. It measures without Vulkan validation by
default so `--gpudbg` layer work does not dominate the timing evidence. Run the correctness-oriented
variant explicitly when needed:

```bash
python launcher.py hybrid-shadow-boundary --config dbg --gpu-validation
```

Forward runner options after `--`:

```bash
python launcher.py hybrid-shadow-boundary -- --measure-seconds 30 --minimum-samples 12
```

After the requested warmup, the runner waits for the next complete timing-file flush and excludes every byte through
that boundary from the measured sample set. The default success condition then requires six positive `render.frame`
and `render.shadow_opaque_trace` intervals from
each arm, plus six positive `render.shadow_transparent_trace` and `render.shadow_transparent_resolve` intervals from
the healthy arm. The opaque baseline must publish no positive transparent timing. Expected arm markers, clean
runtime/validation logs, and nonblank native-window captures remain mandatory. A machine without RayQuery-capable
hardware returns the standard skip code `77`; a missing route marker or runtime error is still a failure.

The report records healthy-hybrid overhead relative to the opaque baseline but does not classify it by default
because the scene changes material classes. If a target has an agreed maximum hybrid overhead, make it explicit:

```powershell
python launcher.py hybrid-shadow-boundary -- --maximum-hybrid-frame-regression-percent 3.0
```

The report compares the mandatory route scopes plus any additional published shadow/tail scopes. A missing optional
scope is never silently converted into zero cost. Artifacts include
per-arm timing files, complete logs, BMP captures, and `hybrid_shadow_boundary_report.json` / `.md`.
Run on a target GPU with a visible native X11/Xwayland or Windows window; this is a hardware evidence
workflow, not a headless CTest.
