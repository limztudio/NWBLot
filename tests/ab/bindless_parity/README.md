# Bindless parity captures

This manual workflow compares the current hardware and forced-software smoke paths that share the descriptor-buffer bindless ABI. It builds both arms through the repository launcher, uses the shared native-window capture backend on Windows and Linux, and keeps every result local under `.cozter/out/ab-results/`.

From the repository root, choose one of the stable scene profiles:

```text
python launcher.py bindless-parity soft-shadows
python launcher.py bindless-parity caustics
python launcher.py bindless-parity surfel-gi
```

The run writes `hardware.bmp`, `software.bmp`, an amplified `difference.bmp`, and `report.json`. By default it reports the measured difference without declaring a pass or failure: caustics and surfel GI have intentional temporal noise floors. Supply explicit limits for a known device when a gate is needed, for example:

```text
python launcher.py bindless-parity soft-shadows --require-exact
python launcher.py bindless-parity caustics --maximum-mean-abs 2.0 --maximum-changed-fraction 0.25
```

The soft-shadow profile freezes the caster yaw and the caustic profile freezes the refractor angle. The runner rejects Vulkan validation errors and falls back to the standard smoke skip code when the host cannot provide a native capture backend.
