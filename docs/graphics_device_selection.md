# Graphics device selection

The selected Vulkan backend uses installed Vulkan drivers for both hardware and CPU rendering. Software fallback does not install or bundle an ICD, emulate missing Vulkan features, or change graphics providers.

## Automatic and explicit selection

With `DeviceCreationParameters::adapterIndex == -1`, initialization chooses the first compatible discrete GPU, otherwise the first compatible non-CPU device, otherwise the first compatible CPU device. Integrated and virtual GPUs take priority over CPU devices regardless of enumeration order. The policy applies to both headless and windowed devices.

Every candidate must satisfy the same required extensions, mandatory features, and graphics/compute queue requirements. Windowed candidates must also provide presentation support and the requested swapchain capabilities. An incompatible hardware candidate does not prevent a compatible software candidate from being selected. An explicitly requested adapter index restricts selection to that adapter; it is never silently replaced with another device.

Selecting a CPU device emits an informational startup message identifying it and explaining that rendering runs on the CPU. Missing or incompatible drivers fail initialization with diagnostics. Software rendering can be substantially slower, and optional capabilities remain governed by the selected device's support.

## Descriptor storage

Resource and sampler descriptor buffers start with bounded working-size reservations. Before the first heap allocation, the backend checks the actual descriptor-set sizes returned by the driver, including alignment and reserved in-flight acceleration-structure blocks. A larger driver representation can increase the initial reservation without reducing the configured descriptor capacity.

Reservations obey checked 32-bit offsets, per-type descriptor ranges and address spaces, and the combined descriptor address-space limit. Growth is allowed only before either segment has published an allocation in the current manager generation. Subsequent allocations retain stable buffer addresses; freeing blocks does not permit relocation. A failed startup replacement invalidates the manager rather than exposing partially initialized storage.

## Verification

Build and run the ordinary selected-backend UI smoke tests:

```sh
python -m launcher build nwb_ui_layer_smoke --arch x64 --config dbg
ctest --test-dir __cmake/build/linux-clang-x64 -C dbg \
  -R '^nwb_ui_layer_(framebuffer|window)_smoke$' --output-on-failure
```

On Linux, use an X11 display or Xvfb and set `VK_DRIVER_FILES` to an installed compatible software ICD manifest to exercise CPU-only initialization. These tests retain the normal loader, global descriptor heap, shaders, rendering, pixel assertions, native input and validation checks. Verify the startup log identifies the software fallback and that the tests pass rather than skip.

`nwb_avboit_depth_warp_probe_smoke` additionally exercises descriptor writes and compute dispatch with full readback and sentinel validation. Build `nwb_avboit_depth_warp_probe_smoke` and run its CTest under the same driver environment.

Hardware-preference qualification requires a machine with compatible hardware and software ICDs visible together. Confirm hardware remains selected with either ICD enumeration order. To exercise fallback from incompatible hardware, leave the software ICD visible while using hardware that fails the required capability checks. No required capability should be disabled to make a candidate pass.
