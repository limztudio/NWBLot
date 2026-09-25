# SIMD Load/Store Boundary Audit — whole workspace

Date: 2026-09-25 (UTC)
Workspace: /home/limstudio/WorkStation/NWBLot_maintanance, branch main...origin/main
Rule: Load/Store only in beginner/entry functions. Helpers take/return SIMDVector/SIMDMatrix (calculation only). Stored values use Float#/Int#/UInt# (+Half#/Float3Int/Float3UInt).

## 1. Whole-scope target list (first)

Enumerated every file containing `SIMDVector|SIMDMatrix` (99 files, `grep -rln`, excluding `.git/` and `3rd_parties/`):
- `global/math/` (27 headers incl. split `vector*.h`, `collision*.h`, `bounding_volumes.h`, `convert.h`, `frame.h`; aggregate `global/simdmath.h`).
- `global/mesh/`: `tangent_frame_rebuild.h`, `triangle_area.h`.
- `core/graphics/vulkan/`: `graphics_pipeline.cpp`, `texture_clear_detail.h`.
- `impl/`: `assets_mesh/*`, `ecs_csg/*`, `ecs_mesh/skinning/*`, `ecs_model/*`, `ecs_render/{csg,material,mesh,optics,raytrace,reflection,shared}/*`, `ecs_scene/*`, `ecs_skeleton/*`, `ecs_ui/*` (each file done below).
- `utilities/`: `fbx_to_nwb/*`, `tex_conv/*` (each file done below).
- `CoolStuff/Testbed/runtime.cpp`, `tests/**` (consumers via public Load/Store API, not boundary owners).
- `launcher/`, `pipeline/`, `loader/`: no SIMD hits (no action).

## 2. Per-target audit (each done)

### global/math — convert.h (beginner layer, CORRECT Load/Store site)
- `LoadHalf(Half2U/Half4U)`, `LoadFloat(Float4/f32/Float2U/3U/4U/Float3Int/UInt/Float34/44/33U/34U/44U)`, `LoadInt(Int4/i32/Int2U/3U/4U/UInt4/u32/UInt2U/3U/4U)`, `StoreFloat(...Float4/f32/Float2U/3U/4U/Float34/44/33U/34U/44U + StoreFloatInt)`, `StoreInt(...Int4/i32/Int2U/3U/4U/UInt4/u32/...)`, `StreamFloat(+Fence)`. PASS as beginner layer.
- `SIMDConvertDetail::{MakeF32/MakeU32, StoreF32/StoreU32, LoadFloat3Components/StoreFloat3Components, StoreFloat34Scalar/44Scalar, LoadFloat34Scalar/44Scalar, LoadFloat34Neon/Aligned/44Neon/Aligned, LoadMatrixRow4, LoadFloat34Sse/44Sse, StoreInt3Bits/StoreUInt3Bits, StoreInt4Scalar/StoreInt4Sse}`: private backend implementations called ONLY by the entries above; no calc helper calls them directly. Raw `_mm_load/_mm_store/vld1/vst1` outside `convert.h`: 0 matches. Body-only re-check (signature line excluded): 0 SIMD-signature helpers with local Load/Store in `global/math`. PASS.

### global/math — vector_lane.h / vector_construct.h / vector_matrix.h / vector_transform.h
- `GetLane/GetIntLane` (SIMD->scalar extraction), `StoreLane/StoreIntLane` (delegate to Get-lane), `VectorGetX/Y/Z/W/ByIndex (+Int)`, `VectorGetXPtr/YPtr/ZPtr/WPtr (+Int, scalar out=)`, `VectorReplicatePtr`, `SplatLane`, `StridePointer` (pointer arithmetic only). No Load/Store. PASS.
- `VectorTransformStreamImpl` + `Vector2/3/4*TransformStream` wrappers: load typed storage at loop top, compute in SIMD, store once. Correct beginner site. PASS.

### global/math — bounding_volumes.h + collision_detail.h / collision_aabb.h / collision_plane.h / collision_sdf.h / collision_triangle.h
- Persistent structs store `Float4`/`Float3U`/slopes only (`BoundingSphere.centerRadius`, `BoundingBox.center/extents`, `BoundingOrientedBox.center/extents/orientation`, `BoundingFrustum.origin/orientation + f32 slopes/planes`). PASS.
- `CollisionDetail::*` (SphereCenter/Radius/CenterRadius, BoxCornerOffset, PlaneNormalizeSafe, TransformPlane, PlaneDistance, MinMax/CenterExtents, ExpandMinMax, ClosestPoint, MinMaxIntersects, FastIntersect*, ObbAxes, PointToObbLocal/InsideObb, AabbCorners/ObbCorners/FrustumCorners, FrustumPlanes, CreateSphereFromVectorPoints over `const SIMDVector*` scratch): SIMD in/out only. PASS.
- `Bounding*::{transform, contains, intersects, createMerged, createFrom*, containedBy, getCorners}`: Load storage at top, SIMD math, Store once. PASS.

### global/math — matrix.h / quaternion.h / frame.h / constant.h / type.h / macro.h / vector*.h
- All calc helpers SIMD in/out (+ scalar angle/epsilon/fov/aspect params). No Float#/Int# structs, no Load/Store. `.f[]` accesses are SCALAR-backend branches only. Constants hold `SIMDVectorConstF/I/U/B` tables (register constants, not stored payloads). PASS.

### global/mesh
- `tangent_frame_rebuild.h`: `RebuildTangentFrames` (beginner: Loads vertex position/uv0/normal/tangent storage, delegates math to SIMD-only `ValidInputVertex/AccumulateTriangleTangentFrame/Frame*` helpers, Stores tangent/frame output). Inner helpers SIMD-only. PASS.
- `triangle_area.h`: `__m256d/float64x2_t` f64 area math, scalar in/out (`TriangleAreaNormal64`), no Load/Store of SIMD storage. PASS.

### impl/assets_mesh
- `cook_meshlets.h/.cpp`: `PrecomputeMeshletTriangleData` + `BuildMeshletBounds` are beginner boundaries (Load entry positions once, then SIMD `MakeMeshletPositionVector/MakeMeshletTriangleVectors/CalculateMeshletBounds/AabbTests::*` helpers stay SIMD-only, Store `MeshletBounds{Float4U sphere, conePacked}` persistent storage). `MeshletScoreState/MeshletTriangleVectors/MeshletBoundsCalculation` are cook-scratch (never cross asset/GPU boundary). PASS.
- `runtime_validation.{h,cpp}`, `skin_cook.cpp`, `skin_validation.h`, `meshlet_payload_packing.h`: beginner validation/cook functions Load once, SIMD-only `MakeMeshPositionVector/MakeMeshUvVector/ValidSkinInfluenceWeights/PackMeshletCone*` helpers, no hidden Load/Store. PASS.

### impl/ecs_csg
- `deform_cap_builder.cpp` (`CapNormal/FillCapLoop` beginner boundaries; `ScaleCenterVec/CapCenterNormalVec` SIMD-only), `deform_wall_builder.cpp` (`MixVertices/NormalizeDeformVertex/SplitEdgeVertex` beginner boundaries; `MixAttributeVec/NormalizeDirectionVec/KeepWVec/TangentHandednessVec` SIMD-only), `deform_cutter_field.cpp` (`ShapeDistances` beginner), `shape_registry.cpp` (`LoadBoxHalfExtents/LoadSphereRadius/LoadCapsuleRadiusHalfHeight` explicit Load-named boundaries), `deform_types.h` (`CsgDeformVertex` stores `Float3U/Float4/Float2U`). PASS.

### impl/ecs_mesh + impl/ecs_model + impl/ecs_skeleton
- `skinning/skin_payload.h` (`BuildSkinInfluences/BuildSkinJointPalette` beginner boundaries; `MeshSkinningInfluenceGpu.weight: Float4` storage), `runtime_cache_resources.cpp` (`ValidateRuntimeMeshUploadPayload` beginner), `runtime_cache_source.cpp` (`BuildRuntimeLocalBounds` beginner), `impl/ecs_model/system.cpp` (`StoreObjectWorldTransform` beginner, Stores `Float44`: no SIMD stored), `impl/ecs_skeleton/runtime_helpers.h` (`BuildStoredJointPaletteFromSkeletonPose` beginner). PASS.

### impl/ecs_render
- `mesh/mesh_view_private.h` (`MeshViewGpuData`: `Float44/Float4` storage; `BuildWorldToClipMatrix/BuildViewFrustum*Vectors/BuildMeshViewFrustumVectors/ResolveMeshViewState` Load once at top, SIMD compose, Store once). `mesh/mesh_resources.cpp` (`BuildPositionStreamBounds` beginner). PASS.
- `raytrace/*` (`rt_private.h`: `NwbBvhNodeGpu` stores `Float3UInt`; `SceneBvhPrimitiveCalculation/SceneBvhNodeCalculation` are build-scratch; `buildMeshSwBvhPrepared` takes caller-loaded `aabbMin/aabbMax` SIMD + Stores GPU push-constant storage once; `optical_scene.cpp`, `rt_caustics_emission_targets.cpp`, `rt_swbvh_*` Load at entry, SIMD compose, Store once). PASS.
- `csg/csg_clip_resolve.cpp`, `material/*`, `optics/coincident_volumes.cpp`, `reflection/settings.cpp`, `shared/renderer_scene_private.h`: entry functions Load ECS/storage inputs, SIMD-only clip/material/lighting helpers, Store GPU payload (`Float#`) outputs. PASS.

### impl/ecs_scene + impl/ecs_ui + core + utilities + CoolStuff
- `ecs_scene/camera.{h,cpp}` (`CameraProjection` stores `Float4+f32`; `TryBuildCameraProjectionValues` SIMD-only, `TryBuildCameraProjection` Stores once; `ResolveSceneCameraView` Loads transform storage at boundary), `view.{h,cpp}` (`SceneViewBasis` stores `Float4`; `BuildSceneViewBasisVectors` SIMD-only, `BuildSceneViewBasis` Stores once), `lighting.{h,cpp}` (`SceneLight` stores `Float4+f32`; `BuildDefaultSceneLight/TryBuildSceneLight/GatherSceneLights` Store/Load at boundary). PASS.
- `impl/ecs_ui/system.cpp`, `core/graphics/vulkan/*`, `utilities/fbx_to_nwb/*`, `utilities/tex_conv/*`, `CoolStuff/Testbed/runtime.cpp`: Load at function top from storage/asset buffers, SIMD math, Store once to `Float#`/GPU structs. PASS.
- `tests/**`: public-API consumers only. No action.

## 3. Re-check leftovers
- Raw `_mm_load/_mm_store/vld1/vst1` outside `global/math/convert.h` (all of `global/ impl/ core/ utilities/ launcher/ pipeline/ loader/ CoolStuff/ tests/`): 0 matches.
- Body-only Load/Store scan of SIMD-signature helpers in `global/math/` (signature line excluded to avoid self-name hits): 0 leftovers.
- SIMD-param helpers with hidden Load/Store in body (`impl/`): 6 names flagged by naive scan, all verified as beginner boundaries storing to named storage (`buildMeshSwBvhPrepared` Stores push-constant `aabbMin/aabbMax`; `TryBuildCameraProjection` Stores `CameraProjection.projectionParams`; `BuildDefaultSceneLight/TryBuildSceneLight/GatherSceneLights` Store/Load `SceneLight` storage; `BuildSceneViewBasis` Stores `SceneViewBasis` storage). 0 true helper violations.
- Persistent-struct scan: GPU/asset/ECS structs persist `Float#/Int#/UInt#/Half#` (+f32/u32 scalars); SIMDVector/SIMDMatrix members exist only in cook/build scratch (`MeshletScoreState`, `MeshletTriangleVectors`, `MeshletBoundsCalculation`, `SceneBvhPrimitiveCalculation`, `SceneBvhNodeCalculation`) that never cross a storage boundary. PASS.

## 4. Change made (this pass)
- No source change needed: every SIMD helper already takes/returns SIMDVector/SIMDMatrix (calculation only); Load/Store already sit only in beginner/entry functions; stored values already use Float#/Int#/UInt# (+Half#/Float3Int/Float3UInt). Prior pass had already fixed `VectorGet*Ptr/StoreLane` lane-output helpers.
- This file refreshed (date + whole-workspace target list + per-target results + verification evidence).

## 5. Verification evidence
- `git pull`: already up to date (branch main).
- Configure: `cmake --preset linux-clang-x64 -DNWB_BUILD_TESTS=ON -DNWB_BUILD_PIPELINE=OFF -DNWB_BUILD_UTILITIES=OFF` — exit 0, 0 errors/warnings in `/tmp/cfg.log`.
- Build: `cmake --build --preset linux-clang-dbg --target nwb_math_tests` — exit 0; only `ninja: warning: premature end of file; recovering` (ninja state notice, not a compile warning); 0 compile errors/warnings in `/tmp/math_build.log`.
- Test: `__exec/linux/x64/full/dbg/math_tests` — exit 0, 26/26 `[  OK ]`, 0 failed/error/exception lines in `/tmp/math_test.log`.
- Status: DONE (everything done; nothing left over).
