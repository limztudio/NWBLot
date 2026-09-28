# Standalone custom UI smoke

`nwb_ui_layer_smoke` creates an ECS world with only `UiLayerSystem` and one `UiPaintComponent`, loads `engine/ui/skins/default/atlas`, and selects `UiLayerPresentation::Standalone`. It renders through the custom GPU layer and acquired backbuffer. The project has no scene renderer or ImGui system.

The first two submitted snapshots are empty, exercising the cleared transparent layer before geometry appears. The deterministic scene then includes opaque solids, half-alpha overlap, a quarter-intensity linear gray, a tinted white atlas sprite, three named nine-slice regions, a transparent arrow sprite, and nested clipping. Geometry uses normalized logical coordinates so both initial and resized extents are observable with the same interior probes.

The CMake target cooks only engine asset roots into `Testing/ui_layer_runtime/<configuration>/res`. The source files separate startup configuration, callback lifecycle, ECS world ownership, and paint scene construction. Capture orchestration and pixel acceptance remain separate Python files.

After building `nwb_ui_layer_smoke`, run `ctest -C dbg -R nwb_ui_layer_ --output-on-failure` from the configured build directory. Both tests hold the shared `nwb_display` lock and request GPU validation:

- `nwb_ui_layer_framebuffer_smoke` uses the existing `FramebufferCapture` observer to copy a completed 960x540 acquired backbuffer after 60 presentation frames. It stores `ui_layer.bmp`, the collected launch log, and `pixels.json` under `Testing/smoke/<configuration>/ui_layer_framebuffer`.
- `nwb_ui_layer_resize_smoke` uses the existing desktop capture helper to verify a rendered 960x540 client, resize it to 800x600, confirm the graphics resize marker, and capture the new client. It checks the same pixels before and after resize and stores both BMPs plus `pixels.json` under the corresponding `ui_layer_resize` directory.

The 16 probes check clear margins, draw order, linear alpha blending, one SDR output conversion, sampled skin tint, named atlas colors, sprite transparency, and intersected clips. Solid tolerances are four byte values or less; atlas probes allow compression and filtering differences of up to eight values. Each failure records its expected and observed RGB, location, and tolerance. Shared capture infrastructure rejects warnings, errors, assertions, validation failures, and abnormal shutdown. Unsupported framebuffer readback or unavailable desktop capture is reported as CTest skip code 77.

HDR composition has separate numeric tests in the graphics asset integration suite. This smoke fixes SDR output so framebuffer bytes have deterministic acceptance values on an SDR display.
