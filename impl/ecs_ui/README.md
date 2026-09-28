# Custom UI ECS adapter

`UiLayerSystem` borrows the world, graphics runtime, input dispatcher, clipboard service and asset manager.
It owns `Ui::Context`, `Ui::Builder`, text/paint services and the independent `Ui::GpuRenderer`.
`UiPaintContext.ui` exposes the builder alongside low-level paint/text for custom drawing. Components stay
in `components.h`; CPU UI code includes no ECS/native/ImGui headers and GPU work sees only frozen paint.

Visible `UiPaintComponent` roots are sorted by explicit `order`, then their complete generational EntityID.
The full EntityID scopes each root. Removing/hiding a root retires its input/actions even while a GPU
snapshot is pending. Model callbacks execute on the owning main thread only when a new frame can be built.
A pending generation retries frozen data without rebuilding callbacks or replaying their actions.

The adapter stores a candidate hit layout for the same generation as submitted paint. Final output acceptance
identifies the acquired backbuffer used by that generation. Only the exact native presentation receipt
publishes that layout; a later scene-only frame cannot qualify it. A rejected matching native presentation
abandons the candidate and clears input publication before rebuilding. This is accepted native queueing,
not a claim that a display scan-out completed. Resize/DPI/device boundaries clear stale input geometry.

Native handler dispatch joins outstanding world work before reading roots. System update uses scheduler
component dependencies and never joins its own task. Input coordinates are logical; the OS dispatcher
normalizes DPI. Focus loss is nonconsumable, and key/button releases visit all handlers to clear scene state.
The first held pointer button assigns scene/custom/legacy ownership through release, including wheel input.
Supported held UI keys keep their owner across focus transfer; ordinary scene keys preserve the UI layout.

During migration, an optional borrowed `UiSystem` delegates its input to this adapter. The legacy overlay
composes after the custom layer and its completed visible-window regions therefore take hit priority.
Only one dispatcher handler forwards each legacy event. Focus transfer clears legacy active/input state;
legacy logical mouse positions are converted to its existing pixel coordinates. Its copied hit regions
contain no live window pointers. Call `detachLegacyInput()` before destroying the borrowed legacy system
or removing this adapter while keeping legacy UI; Testbed's joined teardown does this explicitly.

Testbed's scene handler registers behind UI priority. Its camera uses both UI capture queries and clears
held state on native focus loss. The original ImGui controls remain available while the new skinned gallery
provides an interactive checkbox/counter/reset panel. Clipboard and selection remain OS-owned services;
IME and lost-native-capture/pointer-leave notification are separate subsequent OS boundaries.
