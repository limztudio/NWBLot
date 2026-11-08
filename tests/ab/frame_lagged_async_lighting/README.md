# Frame-lagged async-lighting smoke

This is an opt-in hardware check for the one-frame-late async-lighting path
through the set graphics backend. It needs a real native window and a GPU
with its own compute-only queue. The Graphics queue route exits with code
`77` (skip), not a passing async result.

From the repo root, use the build-aware launcher:

```text
python -m launcher frame-lagged-async-lighting
```

The launcher sets up the test targets, builds the smoke exe and cooked assets,
and starts GPU validation by default in `dbg` or `opt`. For a Final run, use
`python -m launcher frame-lagged-async-lighting --config fin --no-gpu-validation`.
The `fin` loader rejects `--gpudbg`. To pass a cycle-runner flag, place it
after `--`. For example,
`python -m launcher frame-lagged-async-lighting -- --transition-timeout 30`.

The app starts with lagged lighting on. The runner only accepts GPU-submit
moves in this order: start, active history use, normal current-frame path
after F1, then a second start and active-history use after F1 turns the flag
back on. It also rejects GPU validation, renderer fix-up, and history-capture
faults.

Run `python tests/ab/frame_lagged_async_lighting/run.py --self-test` to check
cycle order, missing proof, and fail cleanup with no graphics app. CTest also
runs these checks.
