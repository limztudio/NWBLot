The refraction smoke renders a clear IOR 1.5 sphere with zero AVBOIT color coverage in
front of an opaque stripe wall. A red transparent panel sits in front of the glass;
a blue transparent panel sits behind it. The fixed camera and unlit authored BXDF
make the image comparison independent of lighting histories.

Build or open the scene through the normal asset cooking flow:

```powershell
python tests/smoke/launch.py refraction --config dbg
```

Run `nwb_refraction_capture_smoke` through CTest after building
`nwb_refraction_smoke`. `nwb_refraction_gpudbg_capture_smoke` runs the same sequence
with Vulkan validation enabled in `dbg`. Each test captures three swapchain images
after the same accepted frame count: refraction disabled, automatic tracing, and
explicit screen-space tracing. The latter uses the renderer's normal hardware
tracing preference, leaving device capability reporting intact.

The smoke requires successful resolve dispatch diagnostics, normal application
shutdown, and clean runtime/validation logs. Its visual checks require both dark
stripes becoming light and light stripes becoming dark inside the glass, stable
pixels outside its silhouette, and preservation of the foreground red panel.
Both edge directions are necessary so flat tinting or attenuation cannot pass as
refraction. Captures, amplified differences, and numeric measurements are written
under `Testing/smoke/<config>/refraction` in the selected CMake build directory.

To require a hardware ray-query dispatch on a known capable GPU, run
`refraction_capture_smoke.py` with `--require-hardware`, together with its required
`--executable`, `--working-directory`, and `--output-directory` paths. The normal
CTest accepts the engine's natural route on GPUs without ray queries and always
checks the explicitly selected screen-space route. Lack of swapchain readback
support is reported as a skip; validation failures remain failures.

Interactive fixture controls are `NWB_REFRACTION_SMOKE_ENABLED=0` for the baseline
and `NWB_REFRACTION_SMOKE_HARDWARE=0` for screen-space tracing. They call public
renderer settings and exist only in the smoke project.
