# Immutable renderer baselines

This test-owned workflow captures a current renderer image as an immutable reference, then compares a later
renderer capture against that reference without adding a production feature switch or reviving retired renderer
paths. It is the first prerequisite for the redesign's broader legacy-to-graph parity corpus: a known-good revision
and its capture metadata are preserved before a candidate revision is evaluated.

Capture a reference from the repository root:

```text
python launcher.py renderer-baseline transparent-avboit
python launcher.py renderer-baseline static-csg
python launcher.py renderer-baseline skinned-csg
python launcher.py renderer-baseline stress
```

Each run builds only its selected smoke target and writes `baseline.bmp`, `runtime.log`, and `manifest.json` under
`.cozter/out/ab-results/renderer-baseline/<profile>/<timestamp>/`. The manifest records the source revision,
executable checksum, frozen environment, capture settings, and image checksum. Baseline creation refuses a dirty
source worktree, and an existing artifact directory is never overwritten.

Compare a later build against an existing reference:

```text
python launcher.py renderer-baseline transparent-avboit -- \
  --reference-dir .cozter/out/ab-results/renderer-baseline/transparent-avboit/<timestamp> \
  --maximum-mean-abs 2.0 --maximum-changed-fraction 0.02
```

The candidate directory receives `candidate.bmp`, `difference.bmp`, `comparison.json`, and its own capture manifest.
Without thresholds the comparison is report-only; `--require-exact` is suitable only for deterministic paths. A
reference with a different profile, frozen environment, or GPU-validation mode is rejected rather than silently
compared.

The `transparent-avboit` profile is frame-locked at 96 completed render submissions. Its smoke project sets the
test-only `NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME` control, suspends further submission, logs the ready marker,
then allows a fixed 0.75-second present settle while the native event loop remains alive before it captures the
pinned temporal phase. `transparent-avboit` and `skinned-csg` also set the test-only
`NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS=1/60`, so a fixed 96-frame capture advances time-scaled animation by the
same amount on every run. The caustic profile uses the same control with a fixed 360-frame warm-up, replacing its
old wall-clock-only temporal settle. Surfel GI also uses the fixed 360-frame/`1/60` boundary. Other profiles retain
their documented fixed settle duration until they receive an equally explicit temporal capture point. The soft-shadow
profile now uses the same 360-frame/`1/60` test-only boundary for its temporal shadow history.

Available profiles cover opaque sampled images, transparent AVBOIT, static/skinned CSG, soft shadows, caustics,
surfel GI, and the skinned stress scene. Frame-lagged async lighting and dedicated-Transfer evidence retain their
separate topology-gated workflows because a Graphics fallback is not equivalent evidence.
