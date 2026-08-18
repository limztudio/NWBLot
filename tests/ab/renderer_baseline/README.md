# Immutable renderer baselines

This test-owned workflow captures a current renderer image as an immutable reference, then compares a later
renderer capture against that reference without adding a production feature switch or reviving retired renderer
paths. `current-renderer-v1` is the formal forward reference for the graph renderer: it protects approved current
output from later changes, but it is not a retired-legacy parity corpus.

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

## Formal current-renderer corpus

The checked-in [`current_renderer_corpus.json`](current_renderer_corpus.json) pins the eight approved reference
artifacts by source revision and SHA-256, including each baseline BMP, manifest, and runtime log. It also records one
same-revision re-capture per profile and conservative, adapter-local comparison limits derived from that observed
noise. Use it for a formal forward comparison:

```text
python launcher.py renderer-baseline transparent-avboit -- \
  --reference-corpus current-renderer-v1
```

That mode validates the complete reference identity before it starts the candidate, applies the corpus limits by
default, and refuses a caller-supplied limit that would relax the recorded policy. Stricter limits and
`--require-exact` remain allowed. Its `comparison.json` records `reference_corpus: "current-renderer-v1"` and reports
`pass` or `fail`, rather than a generic report-only verdict.

Raw captures deliberately remain ignored A/B artifacts under `.cozter/out/ab-results/`. Preserve the matching
`renderer-baseline/` artifact tree in the project artifact store and restore it at that default path before use, or
point the runner at a restored tree whose immediate children are the profile directories:

```text
python tests/ab/renderer_baseline/run.py --verify-corpus current-renderer-v1
python launcher.py renderer-baseline surfel-gi -- \
  --reference-corpus current-renderer-v1 \
  --corpus-root /mnt/nwb-artifacts/current-renderer-v1
```

`--verify-corpus` is a no-Vulkan integrity check for all eight baseline images, manifests, and logs. Any missing or
changed artifact fails closed. The v1 corpus was captured on AMD BC-250 with RADV GFX1013 on Linux and Vulkan
validation enabled; use it only on that qualified adapter/driver configuration. The runner enforces validation mode,
scene settings, and immutable hashes; adapter identity remains part of the operator's qualified-hardware selection.

Do not edit or overwrite a corpus reference in place. Replacing it requires a new corpus id, clean-revision captures
for every profile, a documented same-revision noise re-capture, a new checksummed registry, and an acceptance-audit
update explaining why the approved current output changed.

The `transparent-avboit` profile is frame-locked at 96 completed render submissions. Its smoke project sets the
test-only `NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME` control, suspends further submission, logs the ready marker,
then allows a fixed 0.75-second present settle while the native event loop remains alive before it captures the
pinned temporal phase. `transparent-avboit` and `skinned-csg` also set the test-only
`NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS=1/60`, so a fixed 96-frame capture advances time-scaled animation by the
same amount on every run. The caustic profile uses the same control with a fixed 360-frame warm-up, replacing its
old wall-clock-only temporal settle. Surfel GI also uses the fixed 360-frame/`1/60` boundary. Other profiles retain
their documented fixed settle duration until they receive an equally explicit temporal capture point. The soft-shadow
profile now uses the same 360-frame/`1/60` test-only boundary for its temporal shadow history, as does the skinned
stress profile at M4's established 96-frame/`1/60` capture point. The stress control is separate from its existing
M4 async-lighting capture contract.

Available profiles cover opaque sampled images, transparent AVBOIT, static/skinned CSG, soft shadows, caustics,
surfel GI, and the skinned stress scene. Frame-lagged async lighting and dedicated-Transfer evidence retain their
separate topology-gated workflows because a Graphics fallback is not equivalent evidence.
