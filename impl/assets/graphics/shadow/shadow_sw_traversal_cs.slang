// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sw_binding_slots.h"

#define NWB_SCENE_SHADING_SET NWB_SW_SHADOW_SET
#define NWB_SCENE_SHADING_BINDING NWB_SW_SHADOW_BINDING_SCENE_SHADING
#define NWB_SCENE_LIGHT_LIST_SET NWB_SW_SHADOW_SET
#define NWB_SCENE_LIGHT_LIST_BINDING NWB_SW_SHADOW_BINDING_LIGHT_LIST

// Point the surface hook's material-constants context (g_NwbMaterialTypedWords + g_NwbMeshInstances, declared in
// mesh/material_typed_bindings.slangi at NWB_MESH_SET / NWB_MESH_BINDING_*) at this pass's binding set so the
// per-hit transmittance dispatch reads them here -- mirroring the NWB_SCENE_*_SET/BINDING redirects above. The
// mesh-set defaults (slots 6/12) collide with this set's visibility + instance-material slots, so they are moved
// onto the SW shadow slot map.
#define NWB_MESH_SET NWB_SW_SHADOW_SET
#define NWB_MESH_BINDING_MATERIAL_TYPED NWB_SW_SHADOW_BINDING_MATERIAL_TYPED
#define NWB_MESH_BINDING_INSTANCE NWB_SW_SHADOW_BINDING_MESH_INSTANCES

#include "../scene/buffer.slangi"
#include "../bvh/bvh_common.slangi"
#include "../common/math.slangi"
#include "instance_material.slangi"
// Soft-shadow ray-jitter sampling: the per-pixel/per-frame low-discrepancy 2D sample + the cone jitter the soft
// directional trace (mode 11) applies to the occlusion ray so a physical sun disk casts a penumbra.
#include "shadow_sampling.slangi"

// The shadow trace's material-constants context (NwbShadowHit + the per-invocation accessors + the surface
// contract + the engine-side shadow physics helpers nwbShadowFresnelTransmittedFraction /
// nwbShadowAbsorptionCoefficient the trace calls) + the cook-generated per-hit surface dispatch
// (nwbShadowDispatchSurface), which #includes each transparent material's .bind + .surface and routes by
// shadowTransmittanceModelId to return that occluder's optical params (ior / transmission).
#include "shadow_surface.slangi"
#include "shadow/generated/transmittance_dispatch.slangi"

// SHARED per-crossing integration + finalize (NwbShadowVisibilityAccumulator / nwbShadowIntegrateCrossing /
// nwbShadowFinalizeVisibility), identical to the hardware any-hit -- pulled in after the surface helpers it depends
// on. The traversal threads one accumulator by reference and the per-light entry finalizes it.
#include "shadow_integrate.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SW_SHADOW_AABB_PADDING 1e-4
#define NWB_SW_SHADOW_BARYCENTRIC_EPSILON 1e-5


[[vk::binding(NWB_SW_SHADOW_BINDING_GBUFFER_WORLD_POSITION, NWB_SW_SHADOW_SET)]]
Texture2D<float4> g_NwbSwShadowGBufferWorldPosition;

[[vk::binding(NWB_SW_SHADOW_BINDING_GBUFFER_NORMAL, NWB_SW_SHADOW_SET)]]
Texture2D<float4> g_NwbSwShadowGBufferNormal;

[[vk::binding(NWB_SW_SHADOW_BINDING_GBUFFER_DEPTH, NWB_SW_SHADOW_SET)]]
Texture2D<float4> g_NwbSwShadowGBufferDepth;

[[vk::binding(NWB_SW_SHADOW_BINDING_SCENE_NODES, NWB_SW_SHADOW_SET)]]
StructuredBuffer<NwbBvhNode, Std430DataLayout> g_NwbSwShadowSceneNodes;

[[vk::binding(NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT, NWB_SW_SHADOW_SET)]]
RWTexture2DArray<float4> g_NwbSwShadowVisibility;

// Stage-2 adaptive transparent shadow scratch: HALF-res colored transmittance, one layer per shadow slot. The coarse
// trace (mode 4) writes one transmittance per 2x2 block here; the adaptive resolve (mode 5) UAV-loads the 2x2
// neighbourhood to interpolate the flat interior / detect edges.
[[vk::binding(NWB_SW_SHADOW_BINDING_COARSE, NWB_SW_SHADOW_SET)]]
RWTexture2DArray<float4> g_NwbSwShadowCoarse;

// Edge-fraction instrumentation counter ([0] = full-res traced rays, [1] = total candidate rays). The resolve
// atomically tallies into it only when collectStats is set; read back to the log to decide the Stage-3 payoff.
[[vk::binding(NWB_SW_SHADOW_BINDING_EDGE_STATS, NWB_SW_SHADOW_SET)]]
RWStructuredBuffer<uint> g_NwbSwShadowEdgeStats;

// Stage-3 compaction: the edge-pixel append counter ([0] atomic append count, [1] clamped trace count), the compacted
// edge-record list (2 u32 per record), and the indirect dispatch-args buffer (3 u32, also created isDrawIndirectArgs so
// dispatchIndirect accepts it). mode 6 appends + mode 7 builds args + mode 8 indirect-traces; reset/cleared each frame.
[[vk::binding(NWB_SW_SHADOW_BINDING_EDGE_COUNTER, NWB_SW_SHADOW_SET)]]
RWStructuredBuffer<uint> g_NwbSwShadowEdgeCounter;

[[vk::binding(NWB_SW_SHADOW_BINDING_EDGE_LIST, NWB_SW_SHADOW_SET)]]
RWStructuredBuffer<uint> g_NwbSwShadowEdgeList;

[[vk::binding(NWB_SW_SHADOW_BINDING_INDIRECT_ARGS, NWB_SW_SHADOW_SET)]]
RWStructuredBuffer<uint> g_NwbSwShadowIndirectArgs;

// Soft directional shadow (Stage 1): the HALF-res RGBA16F Texture2DArray (one layer per shadow slot) the jittered
// opaque directional trace (mode 11) writes ONE cone-jittered visibility sample per half-res pixel into. The separate
// shadow_resolve pipeline then denoises + upsamples it into the full-res visibility above. Only mode 11 touches it.
[[vk::binding(NWB_SW_SHADOW_BINDING_SOFT_HALF, NWB_SW_SHADOW_SET)]]
RWTexture2DArray<float4> g_NwbSwShadowSoftHalf;

// Per-instance record: the affine world->object transform (3 rows) + the slot into the parallel per-mesh
// descriptor arrays. Mirrors the C++ SceneSwBvhInstanceGpu (64 bytes).
struct NwbSwShadowInstance{
    float4 worldToObjectRow0;
    float4 worldToObjectRow1;
    float4 worldToObjectRow2;
    uint meshIndex;
    uint primitiveCount;
    uint pad0;
    uint pad1;
};

[[vk::binding(NWB_SW_SHADOW_BINDING_SCENE_INSTANCES, NWB_SW_SHADOW_SET)]]
StructuredBuffer<NwbSwShadowInstance, Std430DataLayout> g_NwbSwShadowInstances;

[[vk::binding(NWB_SW_SHADOW_BINDING_INSTANCE_MATERIAL, NWB_SW_SHADOW_SET)]]
StructuredBuffer<NwbRtInstanceMaterial, Std430DataLayout> g_NwbSwShadowInstanceMaterial;

// Parallel per-mesh arrays (slot k = mesh k): triangle BVH nodes + raw position / index byte buffers.
[[vk::binding(NWB_SW_SHADOW_BINDING_MESH_NODES, NWB_SW_SHADOW_SET)]]
StructuredBuffer<NwbBvhNode, Std430DataLayout> g_NwbSwShadowMeshNodes[NWB_SW_SHADOW_MAX_MESHES];

[[vk::binding(NWB_SW_SHADOW_BINDING_MESH_POSITIONS, NWB_SW_SHADOW_SET)]]
ByteAddressBuffer g_NwbSwShadowMeshPositions[NWB_SW_SHADOW_MAX_MESHES];

[[vk::binding(NWB_SW_SHADOW_BINDING_MESH_INDICES, NWB_SW_SHADOW_SET)]]
ByteAddressBuffer g_NwbSwShadowMeshIndices[NWB_SW_SHADOW_MAX_MESHES];

// Per-triangle-corner shadow-trace attributes (AttribGpu = Half4U normal @ +0 (8B) + Float2U uv0 @ +8 (8B), 16B
// stride), indexed as triangle*3+corner in lockstep with the triangle index buffer.
[[vk::binding(NWB_SW_SHADOW_BINDING_MESH_ATTRIBUTES, NWB_SW_SHADOW_SET)]]
ByteAddressBuffer g_NwbSwShadowMeshAttributes[NWB_SW_SHADOW_MAX_MESHES];

struct NwbSwShadowPushConstants{
    uint width;
    uint height;
    uint instanceCount;
    // Pass mode (per-mode behaviour detailed at each handler):
    //  2 = MULTIPLY transparent at HALF resolution -- one trace per 2x2 block written to all four full-res pixels,
    //      each folded onto ITS OWN full-res opaque mask. The UNIFORM (non-adaptive) downscale: the Stage-1 baseline
    //      kept for A/B comparison against the adaptive modes 4/5.
    //  3 = OVERWRITE at full resolution, OPAQUE occluders ONLY (binary blocker mask) -- the software path's own opaque
    //      pre-pass, the SW analog of the HW RayQuery opaque mask, run before the transparent multiply.
    //  4 = COARSE transparent trace (Stage-2 adaptive): one trace per 2x2 block written into the coarse buffer
    //      (transmittance only, NOT yet multiplied onto the opaque mask). The cheap base layer the resolve refines.
    //  5 = ADAPTIVE resolve (Stage-2 adaptive): full-res. Reads the 2x2 coarse neighbourhood; where it AGREES (flat
    //      shadow) bilinearly interpolates it (cheap), where it DISAGREES (an edge) re-traces THIS pixel at full res
    //      (sharp). Folds the result onto each full-res pixel's own opaque mask -- so the opaque (hard) shadow stays
    //      full-res while the soft transparent shadow costs one trace per block in the interior and per-pixel only at
    //      its edges. The opaque mask comes from the HW RayQuery pass (hybrid) or the mode-3 pass (no-RT software).
    //  6 = CLASSIFY+FILL (Stage-3 compacted): full-res. Same 2x2 range test as mode 5; FLAT slots interpolate + fold
    //      onto the opaque mask in place (the ~97% fast path). EDGE slots are NOT traced here -- they are appended to
    //      the compacted edge list and the edge texel is left holding the PRISTINE opaque mask (so mode 8's single
    //      overwrite cannot double-multiply the read-modify-write fold); on list OVERFLOW the edge texel degrades to
    //      the interpolated fallback instead.
    //  7 = BUILD-ARGS (Stage-3 compacted): 1 thread. Clamps the append count to edgeCapacity, writes
    //      DispatchIndirectArguments{ceil(count/traceGroupSize),1,1} into the indirect-args buffer + the clamped count.
    //  8 = INDIRECT TRACE+SCATTER (Stage-3 compacted): DispatchIndirect, 1 record per thread. Unpacks the edge record,
    //      re-traces that pixel/light full-res via nwbSwShadowLightVisible, folds onto the pristine opaque mask. Every
    //      lane in a wave is a real edge ray (no divergence); the tile-major append keeps them screen-local.
    //  9 = OPAQUE COARSE (adaptive-opaque): the no-RT opaque pre-pass's coarse layer -- one OPAQUE-only any-hit trace per
    //      2x2 block written (binary 0/1) into the coarse buffer (reuses nwbSwShadowCoarseTrace; the occluder filter makes
    //      it opaque-only so the transmittance is binary). Replaces the front of the full-res mode-3 pass.
    // 10 = OPAQUE ADAPTIVE RESOLVE (adaptive-opaque): full-res. Reads the 2x2 coarse neighbourhood; if the binary values
    //      AGREE (flat interior, fully lit or fully shadowed) writes that value, else (a silhouette edge) re-traces this
    //      pixel full-res. OVERWRITES the visibility (it IS the opaque mask, so the transparent pass multiplies onto it).
    uint multiplyMode;
    // Coarse (half-res) grid extent for modes 4/5/6 (rounded up so a coarse texel always covers its 2x2 block).
    uint coarseWidth;
    uint coarseHeight;
    // Modes 5/6 edge metric: a 2x2 coarse neighbourhood whose worst per-channel transmittance range exceeds this is an
    // EDGE (re-traced at full res); at or below it the block is FLAT and bilinearly interpolated from the coarse buffer.
    float edgeThreshold;
    // Modes 5/6: when non-zero, atomically tally traced-vs-total rays into the edge-stats buffer (measurement only; adds
    // two atomics per lit pixel, so it is left off on the timed/production path and toggled on only to read the fraction).
    uint collectStats;
    // Stage-3 (modes 6/7/8): element capacity of the edge-list buffer in RECORDS (each = NWB_SW_SHADOW_EDGE_RECORD_WORDS
    // u32). mode 6 guards its appended index against this; mode 7 clamps the trace count to it. Beyond it, edges degrade
    // to the interpolated fallback (graceful, never garbage).
    uint edgeCapacity;
    // Stage-3: threads per group of the indirect trace pass (= NWB_SW_SHADOW_TRACE_GROUP = 64). mode 7 divides the count
    // by this to size the indirect dispatch; mode 8 reconstructs its flat record index as groupID.x*traceGroupSize + groupIndex.
    uint traceGroupSize;
    // Soft directional shadow (mode 11): the renderer frame counter, seeding the per-pixel low-discrepancy cone-jitter
    // sample so the single jittered ray walks the sun disk across frames (decorrelated per pixel by a hash rotation).
    uint frameIndex;
};

[[vk::push_constant]]
ConstantBuffer<NwbSwShadowPushConstants> g_NwbSwShadowPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Slab ray/AABB overlap test over the [tMin, tMax] interval. invDir may contain +/-inf for axis-aligned
// directions; the min/max ordering keeps that well-defined for all but the measure-zero on-slab case.
bool nwbSwShadowRayAabb(const float3 origin, const float3 invDir, const float tMin, const float tMax, const float3 boxMin, const float3 boxMax){
    const float3 t0 = (boxMin - NWB_SW_SHADOW_AABB_PADDING - origin) * invDir;
    const float3 t1 = (boxMax + NWB_SW_SHADOW_AABB_PADDING - origin) * invDir;
    const float3 tSmall = min(t0, t1);
    const float3 tLarge = max(t0, t1);
    const float tNear = max(max(tSmall.x, tSmall.y), max(tSmall.z, tMin));
    const float tFar = min(min(tLarge.x, tLarge.y), min(tLarge.z, tMax));
    return tNear <= tFar;
}

// Reciprocal of the ray direction with each component nudged off exactly zero, so an axis-aligned ray gets
// a large-but-finite slope instead of +/-inf. That keeps the slab test from forming inf*0 = NaN when the
// ray origin lies exactly on a box face (a NaN there compares false and would silently drop the shadow).
float3 nwbSwShadowSafeInvDir(const float3 direction){
    const float epsilon = 1e-8;
    const float3 safe = float3(
        abs(direction.x) > epsilon ? direction.x : (direction.x < 0.0 ? -epsilon : epsilon),
        abs(direction.y) > epsilon ? direction.y : (direction.y < 0.0 ? -epsilon : epsilon),
        abs(direction.z) > epsilon ? direction.z : (direction.z < 0.0 ? -epsilon : epsilon)
    );
    return 1.0 / safe;
}

// Affine world->object transform of a point (row-major 3x4; the engine SIMDMatrix rows map onto the rows).
float3 nwbSwShadowTransformPoint(const NwbSwShadowInstance instance, const float3 point){
    return float3(
        dot(instance.worldToObjectRow0.xyz, point) + instance.worldToObjectRow0.w,
        dot(instance.worldToObjectRow1.xyz, point) + instance.worldToObjectRow1.w,
        dot(instance.worldToObjectRow2.xyz, point) + instance.worldToObjectRow2.w
    );
}

// Same transform applied as a direction (no translation). Left UN-normalized on purpose so the ray
// parameter t is preserved between world and object space (object hit at t == world hit at t).
float3 nwbSwShadowTransformVector(const NwbSwShadowInstance instance, const float3 vector){
    return float3(
        dot(instance.worldToObjectRow0.xyz, vector),
        dot(instance.worldToObjectRow1.xyz, vector),
        dot(instance.worldToObjectRow2.xyz, vector)
    );
}

// Transforms an object-space normal to world space for the surface hook (NwbMeshSurface.normal is world space).
// The normal transform is the inverse-transpose of object->world -- the transpose of the world->object 3x3 carried
// in the instance rows; that transpose's i-th component is the dot of the object normal with the i-th COLUMN of the
// world->object 3x3 (the i-th components of the three rows). This matches the hardware any-hit and is the
// geometrically-correct transform; it equals the rasterizer's world-space normal under uniform instance scale (the
// common case) and diverges in direction only under NON-uniform scale (where the rasterizer applies the forward
// map instead) -- latent for transmittance hooks that don't read the normal. nwbSafeNormalize guards the
// degenerate (near-zero / non-finite, e.g. a singular instance transform) result, matching the engine pattern.
float3 nwbSwShadowTransformNormal(const NwbSwShadowInstance instance, const float3 objectNormal){
    const float3 worldNormal = float3(
        dot(float3(instance.worldToObjectRow0.x, instance.worldToObjectRow1.x, instance.worldToObjectRow2.x), objectNormal),
        dot(float3(instance.worldToObjectRow0.y, instance.worldToObjectRow1.y, instance.worldToObjectRow2.y), objectNormal),
        dot(float3(instance.worldToObjectRow0.z, instance.worldToObjectRow1.z, instance.worldToObjectRow2.z), objectNormal)
    );
    return nwbSafeNormalize(worldNormal, float3(0.0, 0.0, 1.0));
}

// Result of a ray/triangle test: whether the ray crossed the triangle inside (tMin, tMax) + the hit's
// barycentrics. (u, v) are the Moeller-Trumbore parameters (weights of v1, v2); w = 1 - u - v is v0's weight, so
// an attribute interpolates as a*w + b*u + c*v. t is the world/object ray parameter at the hit.
struct NwbSwShadowTriangleHit{
    bool hit;
    float t;
    float u;
    float v;
    float w;
};

// Watertight ray/triangle test (Woop/Benthin/Wald 2013), two-sided: an occluder shadows regardless of facing.
// The edge-function sign convention hits exactly one triangle across a shared edge (no float gap, no double count),
// which is the robust replacement for Moeller-Trumbore's barycentric-epsilon fudge + 1/det amplification. NOTE: this is
// a correctness/robustness improvement, NOT a hardware-parity fix -- the driver's opaque intersector still disagrees
// with ANY software intersector by +/-1 crossing at grazing silhouettes (see shadow_rayquery_cs), an inherent hybrid
// residual, invisible for this binary opaque test beyond a sub-pixel edge. Reports a hit strictly inside (tMin, tMax).
NwbSwShadowTriangleHit nwbSwShadowRayTriangle(const float3 origin, const float3 direction, const float tMin, const float tMax, const float3 v0, const float3 v1, const float3 v2){
    NwbSwShadowTriangleHit result;
    result.hit = false;
    result.t = 0.0;
    result.u = 0.0;
    result.v = 0.0;
    result.w = 0.0;

    // Choose the ray's dominant axis as kz so the shear never divides by a near-zero direction component; kx,ky are the
    // other two, swapped to keep the winding sign consistent when dir[kz] < 0.
    const float3 absDir = abs(direction);
    int kz = (absDir.x >= absDir.y) ? ((absDir.x >= absDir.z) ? 0 : 2) : ((absDir.y >= absDir.z) ? 1 : 2);
    int kx = (kz + 1 == 3) ? 0 : (kz + 1);
    int ky = (kx + 1 == 3) ? 0 : (kx + 1);
    const float dirKz = direction[kz];
    if(dirKz < 0.0){
        const int swap = kx;
        kx = ky;
        ky = swap;
    }

    // Shear/scale constants aligning the ray with +kz.
    const float shearX = direction[kx] / dirKz;
    const float shearY = direction[ky] / dirKz;
    const float shearZ = 1.0 / dirKz;

    // Vertices relative to the ray origin, then sheared into ray space (x,y for the edge tests).
    const float3 relA = v0 - origin;
    const float3 relB = v1 - origin;
    const float3 relC = v2 - origin;
    const float ax = relA[kx] - shearX * relA[kz];
    const float ay = relA[ky] - shearY * relA[kz];
    const float bx = relB[kx] - shearX * relB[kz];
    const float by = relB[ky] - shearY * relB[kz];
    const float cx = relC[kx] - shearX * relC[kz];
    const float cy = relC[ky] - shearY * relC[kz];

    // Scaled barycentrics from the three edge functions (u->v0, v->v1, w->v2 weight).
    float u = cx * by - cy * bx;
    float v = ax * cy - ay * cx;
    float w = bx * ay - by * ax;

    // Two-sided: reject only when the edge functions carry MIXED signs (the point is outside the triangle). All-non-
    // negative OR all-non-positive is inside, so a shared edge (some component exactly 0) still resolves to a single
    // owning triangle rather than a gap.
    if((u < 0.0 || v < 0.0 || w < 0.0) && (u > 0.0 || v > 0.0 || w > 0.0))
        return result;

    const float det = u + v + w;
    if(det == 0.0)
        return result;

    // Interpolate the sheared z to the hit distance, sign-correct against det so the (tMin,tMax) test holds for either
    // triangle facing without a divide.
    const float az = shearZ * relA[kz];
    const float bz = shearZ * relB[kz];
    const float cz = shearZ * relC[kz];
    const float tScaled = u * az + v * bz + w * cz;
    const float detSign = (det < 0.0) ? -1.0 : 1.0;
    const float absDet = det * detSign;
    const float tSigned = tScaled * detSign;
    if(tSigned <= tMin * absDet || tSigned >= tMax * absDet)
        return result;

    const float invDet = 1.0 / det;
    result.hit = true;
    result.t = tScaled * invDet;
    result.u = v * invDet; // MT convention: u weights v1
    result.v = w * invDet; // v weights v2
    result.w = u * invDet; // w weights v0
    return result;
}

// Unpacks a per-triangle-corner shadow-trace attribute (AttribGpu) from a per-mesh attribute byte buffer: Half4U
// normal (two packed-half uints) at +0, Float2U uv0 (two floats) at +8. Returns object-space normal in .xyz, uv0.
struct NwbSwShadowVertexAttrib{
    float3 normal;
    float2 uv0;
};

NwbSwShadowVertexAttrib nwbSwShadowLoadVertexAttrib(const uint meshIndex, const uint attributeIndex){
    // meshIndex is non-uniform across the wave (each invocation's ray hits whatever instance/mesh occludes it), so
    // every per-mesh descriptor-array access MUST be tagged NonUniformResourceIndex; without it the index is UB and
    // a wave that straddles a mesh-shadow boundary reads another mesh's descriptor for some lanes.
    const uint nuMesh = NonUniformResourceIndex(meshIndex);
    const uint base = attributeIndex * 16u;
    const uint2 packedNormal = g_NwbSwShadowMeshAttributes[nuMesh].Load2(base);
    NwbSwShadowVertexAttrib attrib;
    attrib.normal = float3(
        nwbUnpackHalf2x16(packedNormal.x),
        nwbUnpackHalf2x16(packedNormal.y).x
    );
    attrib.uv0 = asfloat(g_NwbSwShadowMeshAttributes[nuMesh].Load2(base + 8u));
    return attrib;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Below this per-channel transmittance the occluder ray is effectively fully blocked; traversal stops early.
#define NWB_SW_SHADOW_TRANSMITTANCE_EPSILON 1e-3

// Per-instance occlusion: push the world ray into the instance's object space, then walk that mesh's
// triangle BVH. Each triangle the ray crosses folds one surface crossing into the shared running accumulator (the
// same nwbShadowIntegrateCrossing the hardware any-hit calls) for a transparent occluder, or zeroes the
// transmittance and stops for an opaque occluder.
void nwbSwShadowInstanceOccluded(const uint instanceIndex, const float3 worldOrigin, const float3 worldDirection, const float tMin, const float tMax, inout NwbShadowVisibilityAccumulator acc){
    const NwbSwShadowInstance instance = g_NwbSwShadowInstances[instanceIndex];
    const NwbRtInstanceMaterial material = g_NwbSwShadowInstanceMaterial[instanceIndex];
    const bool transparent = (material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT) != 0u;
    // Occluder-class filter by pass:
    //  - OPAQUE passes (mode 3 full-res pre-pass; modes 9/10 the adaptive-opaque coarse + resolve): skip TRANSPARENT
    //    occluders -> a binary opaque blocker mask.
    //  - TRANSPARENT passes (modes 2/4/5/8): skip OPAQUE occluders -> the opaque shadow already comes from the HW RayQuery
    //    mask (hybrid) or the opaque pre-pass (software), so tracing opaque here would only redundantly re-darken. Tracing
    //    transparent only also avoids walking the opaque meshes' triangle BVHs. (Mode 8 is the Stage-3 indirect re-trace.)
    const uint occluderPassMode = g_NwbSwShadowPushConstants.multiplyMode;
    // OPAQUE passes: the full-res mode-3 pre-pass, the adaptive-opaque coarse+resolve (9/10), and the soft directional
    // half-res trace (11) all cast an OPAQUE-only blocker -> skip transparent occluders.
    if((occluderPassMode == 3u || occluderPassMode == 9u || occluderPassMode == 10u || occluderPassMode == 11u) && transparent)
        return;
    if((occluderPassMode == 2u || occluderPassMode == 4u || occluderPassMode == 5u || occluderPassMode == 8u) && !transparent)
        return;
    // meshIndex selects this instance's slot in the per-mesh descriptor ARRAYS (nodes/indices/positions/attributes).
    // It is NON-UNIFORM across the wave -- different invocations' rays hit different meshes -- so every indexed access
    // must be wrapped in NonUniformResourceIndex() AT THE SUBSCRIPT (the decoration does not reliably survive a copy
    // into a plain uint). Omitting it is undefined behaviour: on a wave that straddles a mesh-shadow boundary the
    // hardware can read a DIFFERENT mesh's BVH/geometry for some lanes, which culls real occluder hits there and
    // leaves an under-occluded (bright) wave-tile-aligned band along the boundary.
    const uint meshIndex = instance.meshIndex;
    const float3 origin = nwbSwShadowTransformPoint(instance, worldOrigin);
    const float3 direction = nwbSwShadowTransformVector(instance, worldDirection);
    const float3 invDir = nwbSwShadowSafeInvDir(direction);

    uint stack[NWB_SW_SHADOW_MESH_STACK_SIZE];
    uint stackPointer = 0u;
    stack[stackPointer++] = 0u; // per-mesh root

    while(stackPointer > 0u){
        const uint nodeIndex = stack[--stackPointer];
        const NwbBvhNode node = g_NwbSwShadowMeshNodes[NonUniformResourceIndex(meshIndex)][nodeIndex];
        if(!nwbSwShadowRayAabb(origin, invDir, tMin, tMax, node.aabbMin, node.aabbMax))
            continue;

        if(nwbBvhNodeIsLeaf(node)){
            const uint triangle = nwbBvhLeafPrimitive(node);
            const uint indexBase = triangle * 3u;
            const uint i0 = g_NwbSwShadowMeshIndices[NonUniformResourceIndex(meshIndex)].Load(indexBase * 4u);
            const uint i1 = g_NwbSwShadowMeshIndices[NonUniformResourceIndex(meshIndex)].Load((indexBase + 1u) * 4u);
            const uint i2 = g_NwbSwShadowMeshIndices[NonUniformResourceIndex(meshIndex)].Load((indexBase + 2u) * 4u);
            const float3 v0 = asfloat(g_NwbSwShadowMeshPositions[NonUniformResourceIndex(meshIndex)].Load3(i0 * 12u));
            const float3 v1 = asfloat(g_NwbSwShadowMeshPositions[NonUniformResourceIndex(meshIndex)].Load3(i1 * 12u));
            const float3 v2 = asfloat(g_NwbSwShadowMeshPositions[NonUniformResourceIndex(meshIndex)].Load3(i2 * 12u));
            const NwbSwShadowTriangleHit triangleHit = nwbSwShadowRayTriangle(origin, direction, tMin, tMax, v0, v1, v2);
            if(triangleHit.hit){
                if(!transparent){
                    // Opaque occluder: light is fully blocked, stop the whole traversal.
                    acc.transmittance = half3(0.0, 0.0, 0.0);
                    return;
                }
                // Transparent occluder: evaluate this material's surface hook for its optical params and integrate
                // this surface crossing into the running visibility, then keep tracing for further occluders. The
                // hook reads the interpolated normal/uv0 + the material-constants context carried by the instance
                // record; dispatched by shadowTransmittanceModelId.
                const uint attributeBase = triangle * 3u;
                const NwbSwShadowVertexAttrib a0 = nwbSwShadowLoadVertexAttrib(meshIndex, attributeBase);
                const NwbSwShadowVertexAttrib a1 = nwbSwShadowLoadVertexAttrib(meshIndex, attributeBase + 1u);
                const NwbSwShadowVertexAttrib a2 = nwbSwShadowLoadVertexAttrib(meshIndex, attributeBase + 2u);
                const float smoothNormalWeight = nwbShadowSmoothNormalWeight(a0.normal, a1.normal, a2.normal);

                // Interpolate the object-space normal, then transform it to world space (the surface contract) with
                // the inverse-transpose of object->world carried in the instance's world->object rows, so a
                // non-identity instance rotation/scale matches the rasterizer's world-space normal.
                const float3 objectNormal = a0.normal * triangleHit.w + a1.normal * triangleHit.u + a2.normal * triangleHit.v;

                NwbShadowHit hit;
                hit.meshInstanceIndex = material.meshInstanceIndex;
                hit.shadowTransmittanceModelId = material.shadowTransmittanceModelId;
                hit.materialConstantByteOffset = material.materialConstantByteOffset;
                hit.uv0 = a0.uv0 * triangleHit.w + a1.uv0 * triangleHit.u + a2.uv0 * triangleHit.v;
                hit.worldNormal = nwbSwShadowTransformNormal(instance, objectNormal);
                hit.worldPosition = worldOrigin + worldDirection * triangleHit.t;
                hit.rayDirection = worldDirection;

                // Evaluate this material's surface hook for its optical params, then fold the crossing into the
                // running accumulator via the SHARED integrator -- byte-identical to the HW any-hit. The GEOMETRIC face
                // normal cross(v1-v0,v2-v0) + the object-space ray `direction` are already in hand;
                // triangleHit.t is the world distance (the ray dir is left un-normalised so its t matches RayTCurrent()).
                // hit.worldNormal stays the SHADING normal for any hook that reads it; the integrator uses geometry
                // for crossing pairing and smooth author normals only for optional Fresnel.
                const NwbMeshSurface surface = nwbShadowDispatchSurface(material.shadowTransmittanceModelId, hit);
                const float3 geometricNormal = cross(v1 - v0, v2 - v0);
                nwbShadowIntegrateCrossing(acc, instanceIndex, surface.ior, surface.transmission, geometricNormal, objectNormal, smoothNormalWeight, direction, triangleHit.t);
                // Cost safeguard for deeply-stacked transparency: stop once effectively opaque, but KEEP the
                // accumulated (near-zero) value -- the finalize applies the same Beer-Lambert term. Hard-zeroing here
                // was order-dependent and could snap an overlap pixel to pure black; matches the HW soft early-out.
                if(max(max(acc.transmittance.x, acc.transmittance.y), acc.transmittance.z) < NWB_SW_SHADOW_TRANSMITTANCE_EPSILON)
                    return;
            }
        }
        else{
            // Conservative on stack exhaustion: a subtree deeper than the traversal stack is treated as a
            // fully-opaque occluder rather than being skipped, so a pathologically unbalanced BVH over-darkens
            // at worst instead of leaking light through unvisited geometry.
            if((stackPointer + 2u) > NWB_SW_SHADOW_MESH_STACK_SIZE){
                acc.transmittance = half3(0.0, 0.0, 0.0);
                return;
            }
            stack[stackPointer++] = node.leftChild;
            stack[stackPointer++] = node.rightChild;
        }
    }
}

// Scene-level occlusion: walk the software scene/instance BVH; each leaf is an instance whose world AABB the
// ray crossed, so refine it with the exact per-mesh triangle traversal and fold its contribution into the
// shared running accumulator. Stops as soon as the transmittance reaches zero (an opaque hit).
void nwbSwShadowOccluded(const float3 origin, const float3 direction, const float tMin, const float tMax, inout NwbShadowVisibilityAccumulator acc){
    if(g_NwbSwShadowPushConstants.instanceCount == 0u)
        return;

    const float3 invDir = nwbSwShadowSafeInvDir(direction);

    uint stack[NWB_SW_SHADOW_SCENE_STACK_SIZE];
    uint stackPointer = 0u;
    stack[stackPointer++] = 0u; // root

    while(stackPointer > 0u){
        const uint nodeIndex = stack[--stackPointer];
        const NwbBvhNode node = g_NwbSwShadowSceneNodes[nodeIndex];
        if(!nwbSwShadowRayAabb(origin, invDir, tMin, tMax, node.aabbMin, node.aabbMax))
            continue;

        if(nwbBvhNodeIsLeaf(node)){
            nwbSwShadowInstanceOccluded(nwbBvhLeafPrimitive(node), origin, direction, tMin, tMax, acc);
            if(max(max(acc.transmittance.x, acc.transmittance.y), acc.transmittance.z) < NWB_SW_SHADOW_TRANSMITTANCE_EPSILON)
                return;
        }
        else{
            // Conservative on stack exhaustion (see nwbSwShadowInstanceOccluded): an over-deep scene tree is
            // treated as a fully-opaque occluder rather than dropping instances. Unreachable in practice.
            if((stackPointer + 2u) > NWB_SW_SHADOW_SCENE_STACK_SIZE){
                acc.transmittance = half3(0.0, 0.0, 0.0);
                return;
            }
            stack[stackPointer++] = node.leftChild;
            stack[stackPointer++] = node.rightChild;
        }
    }
}

// Per light: cast one occlusion ray from the surface toward the light. Mirrors the hardware raygen
// (directional aims along the toward-light axis to "infinity"; point/spot aim at the light with tMax = the
// distance; geometric-normal bias + tMin avoids self-intersection; a light inside the bias shell is lit).
// Returns the per-channel transmittance: float3(1) when fully lit, tinted toward zero through occluders.
// `jitter` is the per-pixel low-discrepancy 2D sample (in [0,1)^2) for the soft directional cone-jitter; pass
// float2(0,0) for the HARD path (all non-soft modes) -- jitter is only consumed for a directional light with a
// non-zero angular radius (params2.x), where the toward-light direction is offset inside the sun-disk cone so the
// occlusion ray samples the physical source and a penumbra emerges. Point/spot lights ignore the jitter this stage.
half3 nwbSwShadowLightVisible(const float3 worldPosition, const float3 normal, const NwbSceneLight light, const float2 jitter){
    float3 toLight;
    float tMax;
    if(light.params.y < 0.5){
        // Directional: jitter the toward-light axis inside the cone of half-angle params2.x (the sun angular radius).
        // halfAngle 0 (a point-source directional) returns the axis exactly -> the hard shadow, no branch needed.
        toLight = nwbShadowJitterCone(normalize(light.direction.xyz), light.params2.x, jitter);
        tMax = 1.0e7;
    }
    else{
        const float3 delta = light.position.xyz - worldPosition;
        tMax = length(delta);
        toLight = (tMax > 1.0e-5) ? (delta / tMax) : float3(0.0, 1.0, 0.0);
    }

    const float tMin = 0.001;
    const float tEnd = tMax - 0.002;
    if(tEnd <= tMin)
        return half3(1.0, 1.0, 1.0);

    const float3 origin = worldPosition + normal * tMin;
    NwbShadowVisibilityAccumulator acc = nwbShadowVisibilityBegin();
    nwbSwShadowOccluded(origin, toLight, tMin, tEnd, acc);
    // Combine into the final per-channel visibility (Fresnel product * Beer-Lambert, with the absolute-t bias
    // correction) -- the SAME shared finalize the HW raygen uses, so the two backends match exactly.
    return nwbShadowFinalizeVisibility(acc);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Mode 4 -- COARSE transparent trace. One thread per coarse (half-res) texel: trace ONE transparent occlusion ray from
// the 2x2 block's top-left full-res G-buffer texel and store the colored transmittance into the coarse buffer. This is
// the cheap base layer the adaptive resolve (mode 5) interpolates over the flat interior and refines at edges.
void nwbSwShadowCoarseTrace(const uint2 coarsePixel){
    if(coarsePixel.x >= g_NwbSwShadowPushConstants.coarseWidth || coarsePixel.y >= g_NwbSwShadowPushConstants.coarseHeight)
        return;
    // Coarse texel k samples (and the resolve reconstructs around) full-res k*FACTOR -- the block's top-left texel.
    // coarseWidth is rounded UP from width/FACTOR, so k*FACTOR stays in [0, width) for every k < coarseWidth.
    const uint2 base = coarsePixel * NWB_SW_SHADOW_COARSE_FACTOR;
    const int3 texel = int3(int2(base), 0);

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);

    // Background: store fully-lit WHITE so the resolve's range test sees the geometry/background block boundary as an
    // edge (and re-traces it at full res) rather than bleeding "no shadow" across it.
    if(g_NwbSwShadowGBufferDepth.Load(texel).r >= 0.999999){
        for(uint i = 0u; i < lightCount; ++i){
            const float slot = g_NwbSceneLights[i].params.z;
            if(slot < 0.0)
                continue;
            g_NwbSwShadowCoarse[uint3(coarsePixel, uint(slot))] = float4(1.0, 1.0, 1.0, 1.0);
        }
        return;
    }

    const float3 worldPosition = g_NwbSwShadowGBufferWorldPosition.Load(texel).xyz;
    const float3 normal = normalize(g_NwbSwShadowGBufferNormal.Load(texel).xyz * 2.0 - 1.0);
    for(uint i = 0u; i < lightCount; ++i){
        const float slot = g_NwbSceneLights[i].params.z;
        if(slot < 0.0)
            continue;
        const half3 transmittance = nwbSwShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i], float2(0.0, 0.0));
        g_NwbSwShadowCoarse[uint3(coarsePixel, uint(slot))] = float4(transmittance, 1.0);
    }
}

// Mode 5 -- ADAPTIVE resolve. One thread per FULL-res pixel: read the 2x2 coarse neighbourhood; where the colored
// transmittance AGREES (flat shadow) bilinearly interpolate it (cheap), where it DISAGREES (an edge) re-trace THIS pixel
// at full res (sharp). Either way fold the result onto this pixel's own full-res opaque mask, so the opaque (hard)
// shadow stays full-res while the soft transparent shadow costs one trace per block in the interior and per-pixel only
// along its edges. Interpolating the SHADOW signal itself (not a geometry proxy) is what keeps the interpolated value
// correct regardless of the surface under it: where the 4 coarse samples agree the result is that shared value; where
// they disagree we never interpolate -- we trace.
void nwbSwShadowAdaptiveResolve(const uint2 pixel){
    if(pixel.x >= g_NwbSwShadowPushConstants.width || pixel.y >= g_NwbSwShadowPushConstants.height)
        return;
    const int3 texel = int3(int2(pixel), 0);

    // Background: the opaque pass already left it white; nothing to multiply, and it casts no candidate rays.
    if(g_NwbSwShadowGBufferDepth.Load(texel).r >= 0.999999)
        return;

    const float3 worldPosition = g_NwbSwShadowGBufferWorldPosition.Load(texel).xyz;
    const float3 normal = normalize(g_NwbSwShadowGBufferNormal.Load(texel).xyz * 2.0 - 1.0);

    // Continuous coarse coordinate: coarse sample k sits at full-res 2k, so a full-res pixel maps to k = pixel/2. floor +
    // frac give the bracketing coarse texels and the bilinear weights; clamp the upper texel to the coarse extent.
    const float2 coarseCoord = float2(pixel) * (1.0 / float(NWB_SW_SHADOW_COARSE_FACTOR));
    const uint2 c0 = uint2(floor(coarseCoord));
    const uint2 c1 = min(c0 + uint2(1u, 1u), uint2(g_NwbSwShadowPushConstants.coarseWidth - 1u, g_NwbSwShadowPushConstants.coarseHeight - 1u));
    const float2 w = coarseCoord - float2(c0);

    const float edgeThreshold = g_NwbSwShadowPushConstants.edgeThreshold;

    uint tracedRays = 0u;
    uint totalRays = 0u;

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        const float slotF = g_NwbSceneLights[i].params.z;
        if(slotF < 0.0)
            continue;
        const uint slot = uint(slotF);
        ++totalRays;

        // 2x2 coarse neighbourhood transmittances around this pixel.
        const float3 t00 = g_NwbSwShadowCoarse[uint3(c0.x, c0.y, slot)].rgb;
        const float3 t10 = g_NwbSwShadowCoarse[uint3(c1.x, c0.y, slot)].rgb;
        const float3 t01 = g_NwbSwShadowCoarse[uint3(c0.x, c1.y, slot)].rgb;
        const float3 t11 = g_NwbSwShadowCoarse[uint3(c1.x, c1.y, slot)].rgb;

        // Edge metric: worst per-channel range across the 4 coarse samples.
        const float3 lo = min(min(t00, t10), min(t01, t11));
        const float3 hi = max(max(t00, t10), max(t01, t11));
        const float range = max(max(hi.x - lo.x, hi.y - lo.y), hi.z - lo.z);

        float3 transmittance;
        if(range > edgeThreshold){
            // Edge block: re-trace this exact full-res pixel.
            transmittance = float3(nwbSwShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i], float2(0.0, 0.0)));
            ++tracedRays;
        }
        else{
            // Flat interior: bilinearly interpolate the coarse transmittance.
            const float3 tx0 = lerp(t00, t10, w.x);
            const float3 tx1 = lerp(t01, t11, w.x);
            transmittance = lerp(tx0, tx1, w.y);
        }

        const float3 opaqueMask = g_NwbSwShadowVisibility[uint3(pixel, slot)].rgb;
        g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(opaqueMask * transmittance, 1.0);
    }

    // Edge-fraction instrumentation: one atomic pair per lit pixel (only when measuring). traced/total over the frame is
    // the fraction of candidate rays that still needed a full-res trace -- the number that decides the Stage-3 payoff.
    if(g_NwbSwShadowPushConstants.collectStats != 0u && totalRays > 0u){
        InterlockedAdd(g_NwbSwShadowEdgeStats[NWB_SW_SHADOW_EDGE_STATS_TRACED], tracedRays);
        InterlockedAdd(g_NwbSwShadowEdgeStats[NWB_SW_SHADOW_EDGE_STATS_TOTAL], totalRays);
    }
}

// Stage-3 edge record packing: word0 = (x<<16)|y (full-res pixel, 16 bits each), word1 = light-loop index.
uint nwbSwShadowPackPixel(const uint2 pixel){
    return (pixel.x << 16) | (pixel.y & 0xFFFFu);
}
uint2 nwbSwShadowUnpackPixel(const uint word){
    return uint2(word >> 16, word & 0xFFFFu);
}

// Mode 6 -- CLASSIFY + FILL (Stage-3 compacted resolve). Byte-identical edge test + interpolation to mode 5, but EDGE
// slots are not traced here: they are atomically APPENDED to the compacted edge list and the edge texel is left holding
// the PRISTINE opaque mask, so the indirect trace pass (mode 8) can read that mask and fold transmittance with a SINGLE
// overwrite -- never double-multiplying mode-3/HW's read-modify-write fold. On list overflow (index >= edgeCapacity) the
// edge texel degrades to the interpolated fallback instead of being left un-tinted.
void nwbSwShadowClassifyFill(const uint2 pixel){
    if(pixel.x >= g_NwbSwShadowPushConstants.width || pixel.y >= g_NwbSwShadowPushConstants.height)
        return;
    const int3 texel = int3(int2(pixel), 0);

    // Background: leave the pristine (white) opaque mask; it casts no candidate rays.
    if(g_NwbSwShadowGBufferDepth.Load(texel).r >= 0.999999)
        return;

    const float2 coarseCoord = float2(pixel) * (1.0 / float(NWB_SW_SHADOW_COARSE_FACTOR));
    const uint2 c0 = uint2(floor(coarseCoord));
    const uint2 c1 = min(c0 + uint2(1u, 1u), uint2(g_NwbSwShadowPushConstants.coarseWidth - 1u, g_NwbSwShadowPushConstants.coarseHeight - 1u));
    const float2 w = coarseCoord - float2(c0);

    const float edgeThreshold = g_NwbSwShadowPushConstants.edgeThreshold;
    const uint edgeCapacity = g_NwbSwShadowPushConstants.edgeCapacity;

    uint tracedRays = 0u;
    uint totalRays = 0u;

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        const float slotF = g_NwbSceneLights[i].params.z;
        if(slotF < 0.0)
            continue;
        const uint slot = uint(slotF);
        ++totalRays;

        const float3 t00 = g_NwbSwShadowCoarse[uint3(c0.x, c0.y, slot)].rgb;
        const float3 t10 = g_NwbSwShadowCoarse[uint3(c1.x, c0.y, slot)].rgb;
        const float3 t01 = g_NwbSwShadowCoarse[uint3(c0.x, c1.y, slot)].rgb;
        const float3 t11 = g_NwbSwShadowCoarse[uint3(c1.x, c1.y, slot)].rgb;

        const float3 lo = min(min(t00, t10), min(t01, t11));
        const float3 hi = max(max(t00, t10), max(t01, t11));
        const float range = max(max(hi.x - lo.x, hi.y - lo.y), hi.z - lo.z);

        if(range > edgeThreshold){
            // EDGE: append (pixel, lightIndex) to the compacted list; DO NOT trace and DO NOT write the texel -- leave
            // the pristine opaque mask for mode 8's single overwrite. Always increment the counter (so the true edge
            // total is measured) but guard the indexed write to the allocated capacity.
            ++tracedRays;
            uint appendIndex;
            InterlockedAdd(g_NwbSwShadowEdgeCounter[NWB_SW_SHADOW_EDGE_COUNTER_APPEND], 1u, appendIndex);
            if(appendIndex < edgeCapacity){
                const uint base = appendIndex * NWB_SW_SHADOW_EDGE_RECORD_WORDS;
                g_NwbSwShadowEdgeList[base] = nwbSwShadowPackPixel(pixel);
                g_NwbSwShadowEdgeList[base + 1u] = i;
            }
            else{
                // OVERFLOW: this edge will not be traced, so degrade it to the interpolated fallback (Stage-1 quality)
                // rather than leaving the un-tinted opaque mask. Near-impossible at the allocated capacity.
                const float3 tx0 = lerp(t00, t10, w.x);
                const float3 tx1 = lerp(t01, t11, w.x);
                const float3 interp = lerp(tx0, tx1, w.y);
                const float3 opaqueMask = g_NwbSwShadowVisibility[uint3(pixel, slot)].rgb;
                g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(opaqueMask * interp, 1.0);
            }
        }
        else{
            // FLAT interior: interpolate the coarse transmittance and fold onto the pristine opaque mask in place.
            const float3 tx0 = lerp(t00, t10, w.x);
            const float3 tx1 = lerp(t01, t11, w.x);
            const float3 interp = lerp(tx0, tx1, w.y);
            const float3 opaqueMask = g_NwbSwShadowVisibility[uint3(pixel, slot)].rgb;
            g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(opaqueMask * interp, 1.0);
        }
    }

    if(g_NwbSwShadowPushConstants.collectStats != 0u && totalRays > 0u){
        InterlockedAdd(g_NwbSwShadowEdgeStats[NWB_SW_SHADOW_EDGE_STATS_TRACED], tracedRays);
        InterlockedAdd(g_NwbSwShadowEdgeStats[NWB_SW_SHADOW_EDGE_STATS_TOTAL], totalRays);
    }
}

// Mode 7 -- BUILD INDIRECT ARGS (Stage-3 compacted). One thread: clamp the append count to the list capacity, write the
// DispatchIndirectArguments{groupsX,1,1} for mode 8 (groupsX = ceil(count / traceGroupSize); 0 when no edges -> a valid
// zero-workgroup indirect dispatch), and stash the clamped count for mode 8's tail guard.
void nwbSwShadowBuildArgs(const uint3 dispatchThreadID){
    if(dispatchThreadID.x != 0u || dispatchThreadID.y != 0u)
        return;
    const uint groupSize = max(g_NwbSwShadowPushConstants.traceGroupSize, 1u);
    uint count = g_NwbSwShadowEdgeCounter[NWB_SW_SHADOW_EDGE_COUNTER_APPEND];
    count = min(count, g_NwbSwShadowPushConstants.edgeCapacity);
    const uint groups = (count + groupSize - 1u) / groupSize;
    g_NwbSwShadowIndirectArgs[0] = groups;
    g_NwbSwShadowIndirectArgs[1] = 1u;
    g_NwbSwShadowIndirectArgs[2] = 1u;
    g_NwbSwShadowEdgeCounter[NWB_SW_SHADOW_EDGE_COUNTER_TRACE] = count;
}

// Mode 8 -- INDIRECT TRACE + SCATTER (Stage-3 compacted). One thread per compacted edge record: unpack the pixel +
// light, re-trace that single light full-res via the UNCHANGED nwbSwShadowLightVisible (preserving the per-mesh
// NonUniformResourceIndex wrapping), and fold onto the pristine opaque mask mode 6 left in the texel. Every lane in the
// wave is a real edge ray (no divergence); the tile-major append keeps the records -- and thus the traced rays -- screen-local.
void nwbSwShadowIndirectTrace(const uint linearId){
    if(linearId >= g_NwbSwShadowEdgeCounter[NWB_SW_SHADOW_EDGE_COUNTER_TRACE])
        return;
    const uint base = linearId * NWB_SW_SHADOW_EDGE_RECORD_WORDS;
    const uint2 pixel = nwbSwShadowUnpackPixel(g_NwbSwShadowEdgeList[base]);
    const uint lightIndex = g_NwbSwShadowEdgeList[base + 1u];
    const NwbSceneLight light = g_NwbSceneLights[lightIndex];
    const uint slot = uint(light.params.z);

    const int3 texel = int3(int2(pixel), 0);
    const float3 worldPosition = g_NwbSwShadowGBufferWorldPosition.Load(texel).xyz;
    const float3 normal = normalize(g_NwbSwShadowGBufferNormal.Load(texel).xyz * 2.0 - 1.0);
    const float3 transmittance = float3(nwbSwShadowLightVisible(worldPosition, normal, light, float2(0.0, 0.0)));

    const float3 opaqueMask = g_NwbSwShadowVisibility[uint3(pixel, slot)].rgb;
    g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(opaqueMask * transmittance, 1.0);
}

// ANTI-POP dilation (in COARSE cells) for the opaque adaptive resolve's edge TEST. A hard opaque shadow is NOT band-
// limited, so the tight (bracketing-2x2) edge test leaves a block in agreement until a sweeping silhouette edge crosses
// a coarse SAMPLE -- the interpolated block then lags ~1 coarse cell and snaps (the visible pop), and sub-cell slivers
// between samples are missed. Widening the edge TEST by this many coarse cells re-traces the block full-res BEFORE the
// edge arrives, so it is already exact as the edge sweeps through. 1 cell removes the dominant sweep pop at ~3x the
// (still small) edge-retrace count; the interpolation itself stays on the tight bracketing 2x2.
#ifndef NWB_SW_SHADOW_OPAQUE_EDGE_DILATE
#define NWB_SW_SHADOW_OPAQUE_EDGE_DILATE 1
#endif

// Mode 10 -- OPAQUE ADAPTIVE RESOLVE. The adaptive-opaque analog of the full-res mode-3 pre-pass: it WRITES the binary
// opaque blocker mask (the later transparent pass multiplies its colored shadow onto it). Mode 9 wrote one OPAQUE-only
// any-hit result (binary 0/1) per 2x2 block into the coarse buffer; here, per pixel/light, read the 2x2 coarse
// neighbourhood -- where the binary values AGREE (flat interior: fully lit or fully shadowed) write that value, else (a
// silhouette edge straddles the block) RE-TRACE this pixel full-res. Like mode 5 but OVERWRITE (it is the mask, not a
// multiply) and binary. The opaque shadow keeps full-res sharp silhouettes (edge blocks re-trace) while flat interior
// regions cost one trace per 2x2 block -- the same coarse+refine economy the transparent pass already gets.
void nwbSwShadowAdaptiveResolveOpaque(const uint2 pixel){
    if(pixel.x >= g_NwbSwShadowPushConstants.width || pixel.y >= g_NwbSwShadowPushConstants.height)
        return;
    const int3 texel = int3(int2(pixel), 0);

    // Background is fully lit: overwrite every slot white (matching the full-res mode-3 pre-pass), so the transparent
    // pass leaves the white background untouched.
    if(g_NwbSwShadowGBufferDepth.Load(texel).r >= 0.999999){
        for(uint slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot)
            g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    const float3 worldPosition = g_NwbSwShadowGBufferWorldPosition.Load(texel).xyz;
    const float3 normal = normalize(g_NwbSwShadowGBufferNormal.Load(texel).xyz * 2.0 - 1.0);

    const float2 coarseCoord = float2(pixel) * (1.0 / float(NWB_SW_SHADOW_COARSE_FACTOR));
    const uint2 c0 = uint2(floor(coarseCoord));
    const uint2 c1 = min(c0 + uint2(1u, 1u), uint2(g_NwbSwShadowPushConstants.coarseWidth - 1u, g_NwbSwShadowPushConstants.coarseHeight - 1u));
    const float2 w = coarseCoord - float2(c0);

    const float edgeThreshold = g_NwbSwShadowPushConstants.edgeThreshold;

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        const float slotF = g_NwbSceneLights[i].params.z;
        if(slotF < 0.0)
            continue;
        const uint slot = uint(slotF);

        // 2x2 coarse neighbourhood (binary opaque blocker values written by mode 9) -- used for the flat-interior interp.
        const float3 t00 = g_NwbSwShadowCoarse[uint3(c0.x, c0.y, slot)].rgb;
        const float3 t10 = g_NwbSwShadowCoarse[uint3(c1.x, c0.y, slot)].rgb;
        const float3 t01 = g_NwbSwShadowCoarse[uint3(c0.x, c1.y, slot)].rgb;
        const float3 t11 = g_NwbSwShadowCoarse[uint3(c1.x, c1.y, slot)].rgb;

        // Edge TEST over a DILATED coarse neighbourhood (anti-pop, see NWB_SW_SHADOW_OPAQUE_EDGE_DILATE): re-trace where a
        // hard-shadow edge lies within the dilated band of this pixel, so a sweeping edge is caught full-res before it
        // crosses the next sample. The interpolation below still uses ONLY the tight bracketing 2x2 (t00..t11).
        float3 lo = float3(1.0, 1.0, 1.0);
        float3 hi = float3(0.0, 0.0, 0.0);
        const int coarseW = int(g_NwbSwShadowPushConstants.coarseWidth);
        const int coarseH = int(g_NwbSwShadowPushConstants.coarseHeight);
        [unroll] for(int dy = -NWB_SW_SHADOW_OPAQUE_EDGE_DILATE; dy <= 1 + NWB_SW_SHADOW_OPAQUE_EDGE_DILATE; ++dy){
            [unroll] for(int dx = -NWB_SW_SHADOW_OPAQUE_EDGE_DILATE; dx <= 1 + NWB_SW_SHADOW_OPAQUE_EDGE_DILATE; ++dx){
                const int sx = clamp(int(c0.x) + dx, 0, coarseW - 1);
                const int sy = clamp(int(c0.y) + dy, 0, coarseH - 1);
                const float3 s = g_NwbSwShadowCoarse[uint3(uint(sx), uint(sy), slot)].rgb;
                lo = min(lo, s);
                hi = max(hi, s);
            }
        }
        const float range = max(max(hi.x - lo.x, hi.y - lo.y), hi.z - lo.z);

        float3 transmittance;
        if(range > edgeThreshold){
            // Silhouette edge: re-trace this exact full-res pixel (opaque-only -> binary).
            transmittance = float3(nwbSwShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i], float2(0.0, 0.0)));
        }
        else{
            // Flat interior: the agreed binary value (bilinear of equal values == that value).
            const float3 tx0 = lerp(t00, t10, w.x);
            const float3 tx1 = lerp(t01, t11, w.x);
            transmittance = lerp(tx0, tx1, w.y);
        }

        g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(transmittance, 1.0); // OVERWRITE -- this IS the opaque mask
    }
}

// Per-frame samples-per-pixel for the mode-11 soft directional trace. Stage 1 has NO temporal accumulation, so it
// multi-samples the sun disk PER FRAME to hand the resolve a smooth (rather than dithered 0/1) coverage signal the
// value-aware a-trous can actually denoise; Stage 3's temporal accumulation will supply the samples over frames
// instead, letting this return toward 1.
#define NWB_SW_SHADOW_SOFT_SPP 8u

// Mode 11 -- SOFT DIRECTIONAL half-res trace (Stage 1 of the soft-ray-traced-shadow feature). One thread per HALF-res
// pixel: for each DIRECTIONAL light holding a shadow slot, cast ONE OPAQUE occlusion ray CONE-JITTERED inside the
// light's angular radius (params2.x) and write the single-sample visibility (0..1, here binary per sample) into the
// half-res soft buffer. The separate shadow_resolve pipeline then denoises + upsamples it into the full-res
// visibility. Non-directional lights are SKIPPED here (they keep their hard shadow from the opaque/HW pass -- this
// stage softens directional only); the resolve upsample overwrites only the directional slots it processed. The
// jitter is the per-pixel/per-frame low-discrepancy sample, seeded by the half-res pixel + frameIndex so each pixel
// walks the sun disk over frames while neighbours stay decorrelated (the denoise then averages independent estimates).
void nwbSwShadowSoftDirectionalTrace(const uint2 halfPixel){
    // The half-res grid is width>>SOFT_SHIFT: guard against the padded tail. The trace samples the block's top-left
    // full-res texel (matching the geometry downsample's + resolve's 2x point sample).
    const uint halfWidth = (g_NwbSwShadowPushConstants.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) >> NWB_SW_SHADOW_SOFT_SHIFT;
    const uint halfHeight = (g_NwbSwShadowPushConstants.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) >> NWB_SW_SHADOW_SOFT_SHIFT;
    if(halfPixel.x >= halfWidth || halfPixel.y >= halfHeight)
        return;

    const uint2 base = halfPixel << NWB_SW_SHADOW_SOFT_SHIFT;
    const int3 texel = int3(int2(min(base.x, g_NwbSwShadowPushConstants.width - 1u), min(base.y, g_NwbSwShadowPushConstants.height - 1u)), 0);

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);

    // Background: fully LIT (white) for every directional slot -- the multiplicative identity the resolve carries and
    // the value the upsample writes for a background full-res pixel, so the two agree at the geometry/background edge.
    if(g_NwbSwShadowGBufferDepth.Load(texel).r >= 0.999999){
        for(uint i = 0u; i < lightCount; ++i){
            if(g_NwbSceneLights[i].params.y >= 0.5) // directional only (params.y < 0.5)
                continue;
            const float slot = g_NwbSceneLights[i].params.z;
            if(slot < 0.0)
                continue;
            g_NwbSwShadowSoftHalf[uint3(halfPixel, uint(slot))] = float4(1.0, 1.0, 1.0, 1.0);
        }
        return;
    }

    const float3 worldPosition = g_NwbSwShadowGBufferWorldPosition.Load(texel).xyz;
    const float3 normal = normalize(g_NwbSwShadowGBufferNormal.Load(texel).xyz * 2.0 - 1.0);
    for(uint i = 0u; i < lightCount; ++i){
        if(g_NwbSceneLights[i].params.y >= 0.5)
            continue; // directional only
        const float slot = g_NwbSceneLights[i].params.z;
        if(slot < 0.0)
            continue;
        // OPAQUE-only jittered occlusion (the occluder-class filter in nwbSwShadowInstanceOccluded treats mode 11 as an
        // opaque pass). Each sample is binary; averaging NWB_SW_SHADOW_SOFT_SPP of them per frame -- each walking the sun
        // disk via its OWN low-discrepancy 2D point (per pixel, per frame, per sample index) -- yields the smooth 0..1
        // coverage the value-aware resolve needs (a single binary sample would leave a dithered penumbra the a-trous
        // could not smooth without washing the umbra out). Stage 3's temporal accumulation replaces this per-frame loop.
        half3 accum = half3(0.0, 0.0, 0.0);
        [loop] for(uint s = 0u; s < NWB_SW_SHADOW_SOFT_SPP; ++s){
            const float2 sampleJitter = nwbShadowLowDiscrepancy2D(halfPixel, g_NwbSwShadowPushConstants.frameIndex * NWB_SW_SHADOW_SOFT_SPP + s);
            accum += nwbSwShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i], sampleJitter);
        }
        const half3 transmittance = accum * half(1.0 / float(NWB_SW_SHADOW_SOFT_SPP));
        g_NwbSwShadowSoftHalf[uint3(halfPixel, uint(slot))] = float4(transmittance, 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID, uint3 groupID : SV_GroupID, uint groupIndex : SV_GroupIndex){
    const uint multiplyMode = g_NwbSwShadowPushConstants.multiplyMode;

    // Stage-2/3 passes own the thread outright -- their dispatch grids (coarse / full-res / 1D-indirect) differ from the
    // legacy grid and they handle their own bounds.
    if(multiplyMode == 4u){
        nwbSwShadowCoarseTrace(dispatchThreadID.xy);
        return;
    }
    if(multiplyMode == 5u){
        nwbSwShadowAdaptiveResolve(dispatchThreadID.xy);
        return;
    }
    if(multiplyMode == 6u){
        nwbSwShadowClassifyFill(dispatchThreadID.xy);
        return;
    }
    if(multiplyMode == 7u){
        nwbSwShadowBuildArgs(dispatchThreadID);
        return;
    }
    if(multiplyMode == 11u){
        // Soft directional half-res jittered opaque trace (Stage 1). Dispatched at width>>SOFT_SHIFT x height>>SOFT_SHIFT.
        nwbSwShadowSoftDirectionalTrace(dispatchThreadID.xy);
        return;
    }
    if(multiplyMode == 8u){
        // 1D over the compacted edge list: flat record index = groupID.x * traceGroupSize + groupIndex. The group is
        // [numthreads(8,8,1)] = 64 threads and mode 7 sized groupsX = ceil(traceCount / 64).
        nwbSwShadowIndirectTrace(groupID.x * g_NwbSwShadowPushConstants.traceGroupSize + groupIndex);
        return;
    }
    if(multiplyMode == 9u){
        // Adaptive-opaque coarse layer: one OPAQUE-only any-hit trace per 2x2 block (the occluder filter makes it binary).
        nwbSwShadowCoarseTrace(dispatchThreadID.xy);
        return;
    }
    if(multiplyMode == 10u){
        nwbSwShadowAdaptiveResolveOpaque(dispatchThreadID.xy);
        return;
    }

    // Legacy / fallback modes: 2 = uniform HALF-res transparent multiply (the non-adaptive baseline), 3 = full-res
    // OPAQUE-only overwrite (the opaque pre-pass).
    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= g_NwbSwShadowPushConstants.width || pixel.y >= g_NwbSwShadowPushConstants.height)
        return;

    const bool multiply = (multiplyMode == 2u); // mode 3 OVERWRITES; mode 2 MULTIPLIES
    const bool halfRes = (multiplyMode == 2u);

    // In half-res mode the dispatch grid is half-size: each thread owns a 2x2 full-res block, sampling the G-buffer at
    // (and tracing once from) the block's top-left full-res texel.
    const uint2 base = halfRes ? (pixel * 2u) : pixel;
    if(base.x >= g_NwbSwShadowPushConstants.width || base.y >= g_NwbSwShadowPushConstants.height)
        return;
    const int3 texel = int3(int2(base), 0);

    // Background (no geometry) is fully lit; the lighting pass ignores it anyway. In multiply mode the HW opaque pass
    // already wrote the (white) background, so leave it untouched rather than overwrite it.
    if(g_NwbSwShadowGBufferDepth.Load(texel).r >= 0.999999){
        if(!multiply){
            for(uint slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot)
                g_NwbSwShadowVisibility[uint3(base, slot)] = float4(1.0, 1.0, 1.0, 1.0);
        }
        return;
    }

    const float3 worldPosition = g_NwbSwShadowGBufferWorldPosition.Load(texel).xyz;
    const float3 normal = normalize(g_NwbSwShadowGBufferNormal.Load(texel).xyz * 2.0 - 1.0);

    // Trace only the lights that hold a shadow slot (params.z >= 0); write each light's visibility into its
    // own slot layer. Lights without a slot are left at the cleared "all lit" white default.
    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        const float slot = g_NwbSceneLights[i].params.z;
        if(slot < 0.0)
            continue;

        // One transparent traversal (the expensive part) per thread -- shared across the 2x2 block in half-res mode.
        const half3 transmittance = nwbSwShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i], float2(0.0, 0.0));

        if(!multiply){
            g_NwbSwShadowVisibility[uint3(base, uint(slot))] = float4(transmittance, 1.0);
        }
        else{
            // Fold this block's transparent transmittance onto EACH full-res pixel's own opaque binary mask, so the
            // opaque (hard) shadow keeps full resolution while the soft colored transparent shadow is shared 2x2.
            for(uint dy = 0u; dy < 2u; ++dy)
            for(uint dx = 0u; dx < 2u; ++dx){
                const uint2 sub = base + uint2(dx, dy);
                if(sub.x >= g_NwbSwShadowPushConstants.width || sub.y >= g_NwbSwShadowPushConstants.height)
                    continue;
                const float3 opaqueMask = g_NwbSwShadowVisibility[uint3(sub, uint(slot))].rgb;
                g_NwbSwShadowVisibility[uint3(sub, uint(slot))] = float4(opaqueMask * float3(transmittance), 1.0);
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

