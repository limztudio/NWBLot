# AVBOIT refraction

Clear mats with a face IOR above 1.0 take part in camera-seen bend
(`nwbRefractionIorEligible` in
`impl/assets/graphics/refraction/ior.slangi`).
`RendererSystem::setRefractionEnabled(false)` brings back plain AVBOIT.
`setRefractionHardwareTracingEnabled(false)` picks the screen-space path.
It does not change card skill reports. Both picks default to on.

The render keeps one main bend item per pixel. A depth-tested draw pass
grabs its normal, IOR, soak tint, clear-pass item ID, and front-edge bit
(`NWB_REFRACTION_CAPTURE_FRONT_BOUNDARY_FLAG` in bit 16 of
`foregroundColor.x`). It runs after solid shape and clear CSG spans are
ready. Grab uses the same made mat hook and draw order as the other AVBOIT
passes. Its depth face stands apart from solid depth. The grab shader
checks this outright. Clear glass may hold zero `renderCoverage`. Cover is
the AVBOIT see-through knob, not a light-bend mask.

The main item stays out of AVBOIT fill, fade, and color sums. The fix-up
owns its light bend. Other clear bits land in front or back stores per the
grabbed main depth. Back color keeps the old rough AVBOIT bulk weights.
The fix-up takes out the front pass-through first, then reads it. The end
mix puts that pass-through in once. This keeps front dust and panes at
their camera pixels.

On ray-ask cards, the fix-up shares the set scene TLAS, mat store, and kept
shape. It links the draw hit to a TLAS copy. It finds the bulk exit. It
traces the bent back. For one whole non-CSG bend item
(`RtInstanceMaterialFlag::RefractionEntryCertified`) with a front-edge
grabbed pixel, the fix-up rebuilds the exact camera in-spot from grabbed
depth. It starts exit walk straight off with no in-ray ask. Raw,
back-face, or CSG-cut insides send a near-hit in-ask. That ask drops solid
copies from mat flags first, then reads hit facts. It checks the light in
face (`refractionIor`, item ID, world spot). It keeps the main point-fact
slot. Exit tries then load only the 8-byte packed point normals (`Load2`
via `nwbRayTraceDecodeVertexNormal`) or CSG cut face normal. This gives the
world-space exit spot and face normal. It does not reload copy mat notes,
UV facts, or mat face send. The plain path sends two near-hit asks for a
marked one-bender, or three when in-trace is a must. The capped loop takes
at most six or seven. That count holds two in turns and two more plain
clear back coats. Left full in-bounce at this cap ends as solid black. Hard
edge glass can thus show dark chips. The bend fix-up has no in-bounce light
share to win back that power. Soak uses the run length in the main bulk.
Fresnel pass share counts at in and out. Back hits on solid copies check
solid or AVBOIT screen-color reuse straight from hit range and copy flags.
They read this first, before point, center-weight, or `worldToObject` hit
facts. Non-solid copies and raw solid hits rebuild the face once. They call
`nwbRefractionShadeSurfaceHit` only when plain straight soft light is a
must. This is not the full camera mat shader.

The lone screen shader has no ray-ask skill. It walks depth at most 24
times. It falls back to straight-through when it finds no good screen hit.
With no shape exit, it uses a noted one-thick sheet guess for soak. Bad
card setup picks this ready-made path. It keeps a safe stated store set.
CSG mains also use this fall back. The raw tri TLAS does not store their
true/false-cut exits.

This first form leaves out a bounced light lobe. It leaves out spread,
rough-bend reads, noise clean, free nested bend stuff, and exact pass
through a deep clear stack. Fresnel power set for bounce is thus gone from
the shown result. Moved screen-space AVBOIT reuse needs the same main ID.
Past that zone, only solid back reuse or outright traced coats count. These
are set grade caps, not more turns hid behind an open loop.

Grab and fix-up run at full size for crisp glass. The new set-format marks
use near 42 bytes per pixel (grab, bent light, and front sums), plus old
AVBOIT stores. A small sort pass ORs the now full-size grab depth into 8x8
tile live flags. It packs live tile marks. It posts side-call fix-up flags.
Now grab depth gates the end light mix. Pixels past grabbed glass use plain
AVBOIT mix. They do not read bent light. Blank tiles may keep old bytes.
Each now-grabbed pixel is queued and rewritten first. This true-flag deal
skips a full-frame light clear. Each live tile still runs full-size pixels.
It keeps the light in-check. Clear zero-cover glass and half edge tiles
stay in with no new mat pass and no low-size mat reads.

The mark set owns a four-byte note per tile slot. It owns a 32-byte side
flag/mark box (`sparse XYZ`, `activeCount`, `dense XYZ`, plus a kept zero
word). A one-group end pass picks the straight dense path when each tile is
live and tile width fits the send X cap. If not, it picks the packed thin
path (rows capped at 65,535 work groups, with clear tail drop). It zeros
the dead send note. The graph states mark clear, sort and end UAV writes,
shader/side reads, and the out need. Resize swaps these stores with the
grab marks. Mat pass route and the old bounce-try pack stay free of tile
plan. Weigh sort, flag clear, fix-up, mix, and shared ray-box work as one
when you pick a ship grade.

AVBOIT join reuses its packed fade words. It finds the first live depth
word in each low-size XY cell. The forced pass-through clear leaves blank
cells and lead slices at plain one. Join thus skips their powers and bulk
writes. A capped spill slice stays a lone live sign, even when packed adds
rolled a word to zero. This adds no maker locks, mat pass, or held mask
store.

The frame graph states all grab up-loads. It states fake mesh make/draw
edges, face moves, trace ins, and the fix-up/mix need. Resize swaps the
full mark set. Bend off clears grabbed ID/depth and bent true-flags. Old
glass cannot linger.

See [the set smoke test](../tests/smoke/REFRACTION.md) for the
auto off/card/screen check, Vulkan check run, and saved shot scores.
