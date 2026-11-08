# Hardware clear vs solid shadow edge

This hardware A/B flow measures the real hardware clear-shadow route against
a solid hardware-only baseline on the moving ten-figure stress scene. Both
arms freeze the same yaw and use the same binary:

- `nwb_hardware_shadow_boundary_healthy_benchmark` records the healthy
  hardware clear-shadow tail.
- The second run sets
  `NWB_HARDWARE_SHADOW_BOUNDARY_OPAQUE_BASELINE=1`. This makes each test-scene
  figure solid, so the renderer takes hardware shadows with no
  behavior-change hook in production code.

The baseline drops clear mats on purpose. Its capture is a check artifact,
not a look-match input. The runner checks RayQuery and real route markers. It
checks for GPU validation and run faults. It compares GPU time middle values
with no made-up renderer fault.

From the repo root:

```bash
python -m launcher hardware-shadow-boundary
```

Use `--config dbg` to study faults or `--config opt` for timing proof. The
plain launcher rejects `fin` because Final builds drop the warn checks needed
to spot short hardware-route faults. For an opt binary with hashed timing
names, pass its made `.namesym` file with `--healthy-namesym`. Both arms use
the same exe, so the runner uses that file for both arms when
`--baseline-namesym` is left out.

The command sets up and builds the shared check. Then it writes a dated
result pack under `.cozter/out/ab-results/hardware-shadow-boundary/`. It
measures with no GPU validation by default, so `--gpudbg` layer work does not
swamp the timing proof. Run the fault-check form when needed:

```bash
python -m launcher hardware-shadow-boundary --config dbg --gpu-validation
```

Pass runner flags after `--`:

```bash
python -m launcher hardware-shadow-boundary -- --measure-seconds 30 --minimum-samples 12
```

After the set warm-up, the runner waits for the next full timing-file flush.
It drops each byte through that edge from the measured set. Each arm must
then give at least `--minimum-samples` (default six) good `render.frame` and
`render.shadow_opaque_trace` times, plus the same count of good
`render.shadow_transparent_trace` and `render.shadow_transparent_resolve`
times from the healthy arm. Time compares need more than ten good times in
each of those scopes. With fewer, a report with good task gates reads
`inconclusive`. The solid baseline must show no good clear times. Set arm
markers, clean run/check logs, and real native-window shots stay required. A
box with no RayQuery hardware returns the plain skip code `77`. A missing
route marker or run fault is still a fail.

The report notes hardware clear-shadow cost against the solid baseline. It
does not grade it by default because the scene changes mat kinds. If a target
has a set max clear-shadow cost, state it:

```powershell
python -m launcher hardware-shadow-boundary -- --maximum-transparent-frame-regression-percent 3.0
```

The report compares the required route scopes plus any extra shown
shadow/tail scopes. A missing opt scope never quietly turns into zero cost.
Results hold per-arm timing files, full logs, BMP shots, and
`hardware_shadow_boundary_report.json` / `.md`. Run on a target GPU with a
real native X11/Xwayland or Windows window. This is a hardware proof flow,
not a headless CTest.
