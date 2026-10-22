# AVBOIT refraction

Transparent materials with a finite surface IOR above 1.0 participate in camera-visible
refraction (`nwbRefractionIorEligible` in `impl/assets/graphics/refraction/ior.slangi`). `RendererSystem::setRefractionEnabled(false)` restores ordinary AVBOIT;
`setRefractionHardwareTracingEnabled(false)` selects the screen-space route without
changing device capability reporting. Both settings default to enabled.

The renderer retains one primary refractive instance per pixel. A depth-tested
raster pass captures its normal, IOR, absorption tint, and transparent-pass instance
identity after opaque visibility and transparent CSG intervals are ready. Capture
uses the same authored material hook and draw ordering as the other AVBOIT passes.
Its depth attachment is separate from opaque depth, which the capture shader checks
explicitly. Clear glass may have zero `renderCoverage`: coverage is the
AVBOIT opacity parameter, not an optical transmission mask.

The primary instance is omitted from AVBOIT occupancy, extinction, and color
accumulation. The resolve owns its optical transmission. Other transparent
fragments accumulate into foreground or background attachments according to the
captured primary depth. Background color retains the existing approximate AVBOIT
volume weights; the resolve removes the foreground transmittance before sampling
it, and the final composite applies that transmittance once. This keeps foreground
particles and panels at their camera pixels.

On ray-query hardware, the resolve shares the existing scene TLAS, material
context, and retained geometry. It associates the raster entry with a TLAS
instance, finds the volume exit, and traces the transmitted background. The usual
path issues three nearest-hit queries; the bounded loop permits at most seven,
including two internal reflections and two additional ordinary transparent
background layers. Residual total internal reflection at this cap terminates with
opaque black. Hard-edged glass can therefore show dark facets: the refraction
resolve has no internal reflected-radiance contribution to recover that energy.
Absorption uses the traveled distance inside the primary
volume, with Fresnel transmission at entry and exit. Opaque screen color is reused
only after validating visibility against the G-buffer. Other hits receive
simplified direct diffuse lighting. This is not the full camera material shader.

The independent screen shader has no ray-query capability. It performs at most
24 depth-march steps, with a straight-through fallback when it cannot establish a
usable screen hit. Without a geometric exit it uses a documented unit-thickness
sheet approximation for absorption. Hardware preparation failure selects this
already prepared pipeline while retaining a conservative declared resource set.
CSG primaries also use this fallback because the raw triangle TLAS does not encode
their boolean-cut exits.

This first version does not implement a reflected radiance lobe, dispersion,
rough-transmission sampling or denoising, arbitrary nested refractive media, or
exact transmission through an unlimited stack of transparent objects. Fresnel
energy allocated to reflection is therefore absent from the displayed result.
Displaced screen-space AVBOIT reuse requires matching primary identity; outside
that region only opaque background reuse or explicitly traced layers are used.
These are deliberate quality limits, not additional bounces hidden behind an
unbounded loop.

Capture and resolve use full resolution for sharp glass. The new default-format
targets use approximately 42 bytes per pixel (capture, resolved radiance, and the
foreground accumulators), in addition to existing AVBOIT resources. A compute
classifier ORs the current full-resolution capture depth into 8x8 tile activity,
compacts active tile indices, and publishes indirect resolve arguments. Current
capture depth gates the final optical composite: pixels outside captured glass
use ordinary AVBOIT composition without reading resolved radiance. Empty tiles
may retain prior contents; every current captured pixel is queued and rewritten
before composition. This validity contract avoids a full-frame radiance clear.
Each active tile still evaluates full-resolution pixels and retains the optical
entry check. Clear zero-coverage glass and partial edge tiles remain
eligible without a new material pass or reduced-resolution material sampling.

The target generation owns a four-byte entry per possible tile and a 16-byte
indirect argument/counter buffer. Counts beyond 65,535 tiles use a second dispatch
dimension with explicit tail rejection. The graph declares counter clear,
classifier UAV writes, shader/indirect reads, and the output dependency. Resizing
replaces these resources with the capture targets. Material pass routing and the
existing reflection candidate compaction remain independent of tile scheduling.
Measure classifier, argument clear, resolve, composition, and shared
acceleration-structure work together when selecting a shipping quality tier.

AVBOIT integration reuses its packed extinction words to find the first active
depth word in each low-resolution XY cell. The mandatory transmittance clear
leaves empty cells and leading slices at neutral one, so integration skips their
exponentials and volume stores. A finite overflow slice remains an independent
activity signal even when packed additions wrapped a word to zero. This adds no
producer atomics, material pass, or persistent mask allocation.

The frame graph declares all capture uploads, emulated mesh generation/raster
boundaries, attachment transitions, trace inputs, and the resolve/composite
dependency. Resizing replaces the complete target generation; disabling refraction
clears captured identity/depth and resolved validity to prevent stale glass.

See [the dedicated smoke test](../tests/smoke/REFRACTION.md) for the automated
off/hardware/screen comparison, Vulkan validation run, and saved image metrics.
