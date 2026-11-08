# Renderer fault fix (Sept 12, 2026)

This fix mends the faults left open by the [optical look
try](smoke/REFLECTION_OPTICAL_EXPERIMENT.md) plus more faults found by wide
checks. The end full debug run has zero faults. Each fit opt target has a
pass result in the full drive and fix runs. The turned-down optical change
stays out. This fix claims no speed gain. Target counts and headless-GPU
proof below are old: the headless smoke-test face was later cut.

## Fixes and owners

Commit `d6cd6e4f9` fixes renderer hold and the pulled-out source rules:

- `DeferredFrameTailBuilder` takes the task graph and clear frame inputs. It
  no longer leans on or reads closed state in `RendererFramePipeline`. The
  pipe owns graph build, surfel read-back, time notes, and feed-back. The
  maker owns late tail task notes.
- `PrefixSceneUploadBuilder` gets a light-kind still shot. The pipe puts out
  ray-trace kinds after good scene-up notes. It does not show the ray-trace
  core to the up maker.
- Mesh frame-heap free stays closed to its owner.
- At this fix, source-rule tests read the pulled-out code files and checked
  the true call links, order, and hold. They did not paste files into a fake
  old code file or drop the first act checks. Source-wire-only unit checks
  were later cut. Current unit cover uses the act edge-case span in [Test
  plan](README.md).

The smoke-test fix comes after those moves:

- The GPU time fixture acts as a cold record try that notes query need with
  no query pools made. After it drops that try, the next start makes room
  before the measured try. The measured start, full, and drop counts stay 2,
  10, and 10. The test also checks the cold try. It must hold zero noted scopes and two room skips. The read try must add no room skips.
- The clear CSG fixture uses a project-owned plain green mat for its mid
  plate and turns off show/glass for the clip and cover check. This stops
  face light and glass back move from hiding the shape under test. Plain
  clear, glass, show, and mixed shine fixtures keep their own cover.
- Each CSG shot picks its true still pose: early, mid, or late (turn 0, 1, or
  2). The answer file checks inset spots found from the shown plate and kept
  shape. It no longer picks which of three spots holds the most back, which
  could pass a lost cutter. Kept pixels must also name the green plate. Old
  light gaps, fill shares, least pixel counts, settle times, and drop of the
  first bad non-blank frame stay whole.

There were 50 separate seen fail cases: 44 ECS GTest cases, three swap show
cases, one GPU-task pack-in case, one named time case, and the early clear
CSG look smoke. Left faults in mid fixes do not count twice.

## Proof

Proof uses Windows ARM64, the `windows-clang-arm64-opt` and
`windows-clang-arm64-dbg` forms, and the on-hand Qualcomm Adreno X2-90 Vulkan
card. Builds, asset cook, and GPU tests ran one by one.

| Run | Result | Local proof under `__artifacts/renderer_error_cleanup/` |
| --- | --- | --- |
| End full opt build | Passed | `complete_opt_build.log` |
| End full debug build | Passed | `debug_build.log` |
| Full cook of engine + fixture assets (opt and debug) | Passed | `cook_opt.log`, `cook_debug.log` |
| Full opt CTest | Passed | `ctest_opt_full_validation.log` |
| Full debug CTest | Passed | `ctest_dbg_full_validation.log` |
| Debug CSG captures, fixed `nwb_csg_visible_smoke` binary | Passed | `csg_visible_capture_dbg.log` |
| Debug transparent CSG captures, fixed `nwb_transparent_csg_smoke` binary | Passed | `transparent_csg_capture_dbg.log` |

The end full opt CTest ran 203 tests with zero fails. The end full debug
CTest ran 60 tests with zero fails. These runs, the named build logs, and the
stale tries in `m4_round1`-form folders are the proof. Old full-suite counts
stay as past proof. They do not re-prove a later source.
