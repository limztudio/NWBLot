# A/B workflows

Only full workflows that run on their own live here. Run them through the
repository launcher so the same commands work on all hosts:

- `python -m launcher async-shadow-m4` checks the async-compute shadow queue
  against its sync baseline.
- `python -m launcher frame-lagged-async-lighting` checks the opt-in lighting
  history cycle.
- `python -m launcher hardware-shadow-boundary` measures the healthy hardware
  transparent-shadow tail against an opaque hardware-shadow baseline.
- `python -m launcher renderer-baseline <profile>` takes or compares a fixed
  current-renderer scene baseline.

The folder `launch.py` only routes to workflows. Do not add an A/B script that
needs manual source edits, hard-coded build paths, or a platform shell. Keep
raw captures, logs, timing tables, diff images, and result notes under
`.cozter/out/ab-results/`.
