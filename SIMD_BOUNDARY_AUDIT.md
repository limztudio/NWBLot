# SIMD Load/Store Boundary Audit — global/math

Date: 2026-09-18 (UTC)
Workspace: /home/limstudio/WorkStation/NWBLot_maintanance, branch main...origin/main
Rule: Load/Store only in beginner/entry functions. Helpers take/return SIMDVector/SIMDMatrix (calculation only). Stored values use Float#/Int#/UInt# (+Half#/Float3Int/Float3UInt).

## 1. Whole-scope target list (first)

Searched whole tree for `SIMDVector|SIMDMatrix|__m128|float32x4_t|_mm_|vld|vst`:
- Hits ONLY under `global/math/`: type.h, macro.h, constant.h, convert.h, vector.h, quaternion.h, matrix.h, collision.h, collision.inl, frame.h (+ simdmath.h aggregate).
- No hits in `core/**`, `impl/**`, `pipeline/**`, `launcher/**`, `utilities/**`, `loader/**` (SIMDVector/Matrix names). Those layers consume math via Load/Store boundaries and Float#/Int#/UInt# storage.
- `tests/unit/math/math_tests.cpp` uses public API (Load/Store/Vector*/Matrix*/collision entries) — consumer, not a boundary violator.

## 2. Per-file audit (each done)

### type.h
- Storage structs only: Half/Half2U/Half4U, Float4/34/44, Int4, UInt4, Float3Int/UInt, Float2U/3U/4U, Float33U/34U/44U, Int2U/3U/4U, UInt2U/3U/4U + SIMDVector/SIMDMatrix aliases. No load/store. PASS.

### convert.h
- Entry/beginner (typed storage <-> SIMD, CORRECT place for intrinsics): LoadHalf(Half2U/Half4U), LoadFloat(Float4/f32/Float2U/3U/4U/Float3Int/UInt/Float34/44/33U/34U/44U), LoadInt(Int4/i32/Int2U/3U/4U/UInt4/u32/UInt2U/3U/4U), StoreFloat(...Float4/f32/Float2U/3U/4U/Float34/44/33U/34U/44U + StoreFloatInt), StoreInt(...Int4/i32/Int2U/3U/4U/UInt4/u32/...), StreamFloat(+Fence). PASS as beginner layer.
- Detail backend impls (called ONLY by entries above, not by calc helpers): SIMDConvertDetail::MakeF32/MakeU32 (scalar->SIMD constructors), StoreF32/StoreU32 (raw f32*/u32* primitives, currently unused outside detail), StoreInt3Bits/StoreUInt3Bits, LoadFloat3Components/StoreFloat3Components, StoreFloat34Scalar/44Scalar, LoadFloat34Scalar/44Scalar, LoadFloat34Neon/Aligned/44Neon/Aligned, LoadMatrixRow4, LoadFloat34Sse/44Sse, StoreInt4Scalar/StoreInt4Sse. These factor backend (SCALAR/NEON/SSE) code for entries. No calc helper calls them directly. ACCEPTED as private implementation of beginner layer (not a helper bypass).
- Scalar half/unsigned converters (FloatToHalfScalar/HalfToFloatScalar/ConvertFloatToHalf/etc., MakeHalf2U/4U, LoadHalf2U/4U returning Float2U/4U storage): scalar/storage domain, no SIMD. PASS.

### vector.h
- Detail pure (SIMD-only, no memory): ComparisonMaskR/BoundsMaskR/RoundToNearest/ScalarSinCos/TruncateBits/MultiShift/GetLeadingBit/SplatLane/MatrixTranspose4/TransposeForTransform/TransposePackedRows/Vector4TransformTransposed/MatrixDotPack/TrigPolynomials/NormalizeOrV/ClampLengthV/RefractV. PASS.
- Stream entry (typed storage, CORRECT Load/Store site): VectorTransformStreamImpl (LoadFloat+StoreFloat inside loop over InputT/OutputT with stride) + all Vector*TransformStream wrappers (Float2U/3U/4U streams). PASS as beginner.
- Pure calc helpers (SIMD in/out + scalar params like angle/epsilon/index/mask): all Vector2/3/4* arithmetic, compare, round, trig, normalize, reflect/refract, swizzle/permute/merge/select, Matrix* helpers. Scalar f32/u32 params are lane values/thresholds, not stored vectors. PASS.
- Lane Get (SIMD->scalar, no store): GetLane/GetIntLane detail + VectorGetX/Y/Z/W/ByIndex (+Int variants) delegating to them. Scalar return, not storage struct. ACCEPTED (extraction, used for outDistance/epsilon logic in collision/matrix).
- Lane Store (SIMD->caller scalar ref): StoreLane/StoreIntLane detail now delegate to GetLane/GetIntLane (fixed this task, removed direct _mm_store_ss/vst1q_lane duplication). VectorGetXPtr/Y/Z/WPtr + Int variants + ByIndexPtr now route via StoreFloat/StoreInt (lane0) or VectorGet* (lanes1-3). They are scalar-lane store entries, not struct stores. ACCEPTED.
- FIXED this task in vector.h: VectorReplicatePtr/ReplicateIntPtr (const f32&/u32& -> by-value, body now `return VectorReplicate(value)` / `return VectorReplicateInt(value)`, removed direct vld1q_dup/_mm_broadcast_ss/_mm_load_ps1); VectorSetXPtr/YPtr/ZPtr/WPtr + Int variants + ByIndexPtr (const f32&/u32& -> by-value f32/u32, bodies now `return VectorSetX(value,x)` etc., removed direct vld1q_lane/_mm_load_ss/_mm_insert_ps). No helper now locally issues load/store intrinsics; pointer forms are thin by-value forwarders. Verified: grep `\(const f32&|\(const u32&` in global/math = 0 matches after fix.
- Remaining `f32& out / u32& out` signatures are exactly the scalar-lane store entries above + VectorEqualR/GreaterR/etc. (u32& outCR status codes) + ScalarSinCos outs. No struct-store bypass. PASS.

### quaternion.h / matrix.h / frame.h
- All calc helpers SIMD in/out (+ scalar angle/epsilon/fov/aspect params). No Float#/Int# structs, no _mm_load/_mm_store/vld/vst, no LoadFloat/StoreFloat except via Vector* helpers. MatrixPerspectiveImpl/FovImpl take scalar view dims (f32) and build SIMDMatrix via Vector* merges — construction from scalars, not a memory load. PASS.

### collision.h / collision.inl
- Storage: BoundingSphere (Float4 centerRadius), BoundingBox (Float4 center/extents), BoundingOrientedBox (Float4 center/extents + SIMD? check: orientation stored as Float4), BoundingFrustum (Float4 origin/orientation + f32 slopes/planes). All persistent fields Float#/f32. PASS.
- Detail pure (SIMD-only): SphereCenter/Radius/CenterRadius, BoxCornerOffset, PlaneNormalizeSafe, TransformPlane, PlaneDistance, MinMax/CenterExtents, ExpandMinMax, ClosestPoint, MinMaxIntersects, FastIntersect*, ObbAxes, PointToObbLocal/InsideObb, AabbCorners/ObbCorners (SIMDVector* corners scratch, register spill for 8 corners — explicitly corner-array API, not persistent storage), FrustumPlanesIntersectSphere, etc. PASS.
- Entry/beginner (CORRECT Load/Store sites): Bounding*::transform/contains/intersects/createMerged/createFrom*/containedBy etc. Each Loads Float4/Float3U storage at top (`LoadFloat(centerRadius)`, `LoadFloat(box.center)`...), computes in SIMD, Stores via `StoreFloat(..., outSphere.centerRadius)` / `outBox.center` / `outFrustum.*`. Example lines: collision.inl 990-1268, 1238-1268, 1332, 1512-1595, 1795-1842, 2102-2120. No helper does its own Load/Store. StrideFloat3Pointer/StridePointer are pointer-arithmetic only; dereference is always wrapped in LoadFloat at entry. PASS.

## 3. Re-check leftovers
- `\(const f32&|\(const u32&` in global/math: 0 matches (fixed).
- Raw `_mm_load_ss/_mm_load_ps1/vld1q_lane/vld1q_dup/_mm_broadcast_ss` in vector.h: 0 matches after fix (only remain inside convert.h beginner Load* implementations, as intended).
- `StoreF32/StoreU32` raw-pointer primitives: defined in SIMDConvertDetail, no external callers; left as private primitives (not called by calc helpers).
- Storage invariant: grep for persistent structs shows only Float#/Int#/UInt#/Half# + f32/u32 scalars; no SIMDVector/SIMDMatrix stored in Bounding*/asset/ECS structs in math layer.

## 4. Change made (this pass)
- `global/math/vector.h`: `VectorGetXPtr/YPtr/ZPtr/WPtr` now uniformly route via `VectorGetX/Y/Z/W` (scalar `out =` lane extraction); `VectorGetIntXPtr/YPtr/ZPtr/WPtr` now uniformly route via `VectorGetIntX/Y/Z/W`. Removed remaining beginner-Load/Store calls from scalar lane-output helpers so Load/Store (`LoadFloat`/`StoreFloat`/`LoadInt`/`StoreInt` in `convert.h`, stream `VectorTransformStreamImpl` + `Float2U/3U/4U` stream wrappers, `Bounding*` storage members taking/returning `Float4` storage) stay only in entry/beginner functions; calc helpers keep SIMDVector/SIMDMatrix in/out; storage stays Float#/Int#/UInt# (+Half#/Float3Int/Float3UInt). Prior `StoreLane`/`StoreIntLane` dedup + `VectorReplicatePtr`/`VectorReplicateIntPtr` by-value scalar forwarding retained. See `git diff -- global/math/vector.h`.

## 5. Blocked items (no tool capability in this environment)
- TOOLS AVAILABLE: no shell/exec, no git pull/commit/push tool (only git_info status/log/diff read-only), no build runner. Therefore:
  - `git pull` NOT executed (would also require merge: workspace already dirty with ~60 unrelated modified files + 3 untracked before this task; pull would need reconciliation).
  - Build/test verification NOT executed (cannot run cmake/ctest, cannot grep build logs).
  - `commit` + `push to main` NOT executed.
- To finish: run `git pull --rebase`, build math tests (tests/unit/math), run ctest, then `git add global/math/vector.h SIMD_BOUNDARY_AUDIT.md && git commit -m "..." && git push origin main`, and attach push proof.
- Status: PARTIAL + remainder = commit/push/verify pending (human/CI with shell+git access must complete).
