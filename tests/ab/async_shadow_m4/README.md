# Async-shadow M4 hardware check

This check helps decide the M4 rollout. It needs a GPU with its own
compute-only queue. It runs the same fixed-yaw stress scene twice:

- `nwb_async_shadow_m4_sync_benchmark` turns off the async lane request.
- `nwb_async_shadow_m4_async_benchmark` keeps the async-compute lane request.

The test logs the real Graphics and Compute queue IDs and device versions. It
also logs Compute support and whether the queue is dedicated. The runner only
accepts a requested, distinct, current, dedicated Compute queue with no
Graphics support. Shared transport gives a skip. Bad IDs or mixed evidence
give a fail. The sync request stays off even on hardware that needs its own
Compute transport.

The runner rejects an async result that quietly uses the Graphics queue. For a
real dedicated lane, it collects renderer time stamps. It checks that graph
`render.async_shadow` reports work. It compares the `render.frame` Graphics
path, not the sum of queue work. It takes a fixed-scene pixel A/B at the set
fixture hold count in each mode. It scans logs for ownership or backend
validation faults.

From the repo root, use the one-command launcher:

```powershell
python -m launcher async-shadow-m4
```

It sets up the test targets, builds both checks and their cooked assets, turns
on GPU validation by default, and writes a dated folder under
`.cozter/out/ab-results/async-shadow-m4/`. Validation works in `dbg` and
`opt`. For a Final run, use
`python -m launcher async-shadow-m4 --config fin --no-gpu-validation`.
The `fin` loader rejects `--gpudbg`. The command returns `77` when the card
has no distinct dedicated compute-only queue. To change a `run.py` setting,
pass it after `--`. For example:

```powershell
python -m launcher async-shadow-m4 -- --measure-seconds 30
```

Pixel capture and timing run in two steps. `--pixel-capture-frames` defaults
to 96 and sets `NWB_M4_PIXEL_CAPTURE_FREEZE_FRAME`. The fixture counter moves
once per world update. At that count, the capture step stops new render work
but keeps its event loop alive. The runner waits for the ready marker and the
suspend note before it settles and captures the window. This hold does not fix
an exact count of shown frames or the same history phase in both arms. The two
images must still match pixel for pixel. The timing step runs on its own with
no hold. Change the count when you study capture behavior:

```powershell
python -m launcher async-shadow-m4 -- --pixel-capture-frames 128
```

A separate debug run of either check may set `NWB_STRESS_FRAME_GRAPH_FILE` to
an output path with `NWB_M4_PIXEL_CAPTURE_FREEZE_FRAME`. Before it prints the
held-frame marker, the check writes clear JSON plus the coded public stream at
`<path>.nwbs`. This capture needs pixel freeze and rejects present-measure
mode. Keep it unset for normal paired timing runs. The file holds task
placement, packet edges, barrier counts, and accepted submit waits for the
done frame. Submit counts show real waits, same-queue skips, merged dups, and
waits covered by past accepts. Public view edges do not show built state-seed
IDs or extra compile state.

The pixel step does not prove the same history phase. The timing step does not
include capture-step frame counts.

## Timing gate

Both arms must reach a steady state. Each arm needs at least the set count of
good `render.frame` samples. The runner compares middle values of the Graphics
path. It passes only when the full async range sits below the full sync range
by the set margin. Shared warm-up, one-sided drops, or changed fixture counts
void the run. See `run.py --help` for all flags and defaults.

## Files

- `run.py`: the paired runner. It owns setup, launch, log scan, pixel check,
  timing compare, and the report.
- `launch.py`: the launcher entry. It forwards flags to `run.py`.
- `qualification_history.md`: past runs and their results.
