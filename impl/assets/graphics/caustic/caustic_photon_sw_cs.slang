// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sw_binding_slots.h"
#include "../mesh/runtime_constants.h"

#define NWB_SCENE_SHADING_SET NWB_CAUSTIC_SW_SET
#define NWB_SCENE_SHADING_BINDING NWB_CAUSTIC_SW_BINDING_SCENE_SHADING
#define NWB_SCENE_LIGHT_LIST_SET NWB_CAUSTIC_SW_SET
#define NWB_SCENE_LIGHT_LIST_BINDING NWB_CAUSTIC_SW_BINDING_LIGHT_LIST

// Point the surface hook's material-constants context (g_NwbMaterialTypedWords + g_NwbMeshInstances, declared in
// mesh/material_typed_bindings.slangi at NWB_MESH_SET / NWB_MESH_BINDING_*) at this pass's binding set so the
// per-hit surface dispatch (which supplies a refractive occluder's ior / transmission) reads them here -- exactly
// as the SW shadow traversal redirects them onto its own set.
#define NWB_MESH_SET NWB_CAUSTIC_SW_SET
#define NWB_MESH_BINDING_MATERIAL_TYPED NWB_CAUSTIC_SW_BINDING_MATERIAL_TYPED
#define NWB_MESH_BINDING_INSTANCE NWB_CAUSTIC_SW_BINDING_MESH_INSTANCES

#include "../scene/buffer.slangi"
#include "../bvh/bvh_common.slangi"
#include "../bvh/ray_intersect.slangi"
#include "../common/math.slangi"
#include "../shadow/instance_material.slangi"

// Global descriptor heap accessors (Phase 2 step 4b): the per-mesh geometry the photon traversal fetches now lives in
// the bindless heap, reached by the per-buffer slots the instance material record (NwbRtInstanceMaterial) carries.
// bindless_heap_bvh.slangi pulls the structured NwbBvhNode node view (NwbHeapBvhNodes) + the raw byte view
// (NwbHeapRawBuffer) for positions / indices / attributes, both at heap set 8 (pinned on the SW caustic pipeline in 4a).
#include "../bindless/bindless_heap_bvh.slangi"

// The shadow trace's material-constants context (NwbShadowHit + the per-invocation accessors + the surface contract)
// + the cook-generated per-hit surface dispatch (nwbShadowDispatchSurface), which #includes each transparent
// material's .bind + .surface and routes by shadowTransmittanceModelId to return that occluder's optical params
// (ior / transmission). The caustic closest-hit hook evaluates a refractive surface through the IDENTICAL dispatch
// the shadow trace uses, so the two passes agree on a glass's optical params.
#include "../shadow/shadow_surface.slangi"
#include "shadow/generated/transmittance_dispatch.slangi"

// The shared caustic transport layer: the photon struct, the emission aim, the iterative bounce loop, the splat
// math. It declares the backend closest-hit hook nwbCausticTraceClosest, defined below over this pass's BVH fetch.
#include "caustic_trace.slangi"
#include "light_pick.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_CAUSTIC_SW_BINDING_SCENE_NODES, NWB_CAUSTIC_SW_SET)]]
StructuredBuffer<NwbBvhNode, Std430DataLayout> g_NwbCausticSwSceneNodes;

// Per-instance record: the affine world->object transform (3 rows) + the slot into the parallel per-mesh
// descriptor arrays. Mirrors the C++ SceneSwBvhInstanceGpu (64 bytes) / the SW shadow NwbSwShadowInstance.
struct NwbCausticSwInstance{
    float4 worldToObjectRow0;
    float4 worldToObjectRow1;
    float4 worldToObjectRow2;
    uint meshIndex;
    uint primitiveCount;
    uint pad0;
    uint pad1;
};

[[vk::binding(NWB_CAUSTIC_SW_BINDING_SCENE_INSTANCES, NWB_CAUSTIC_SW_SET)]]
StructuredBuffer<NwbCausticSwInstance, Std430DataLayout> g_NwbCausticSwInstances;

[[vk::binding(NWB_CAUSTIC_SW_BINDING_INSTANCE_MATERIAL, NWB_CAUSTIC_SW_SET)]]
StructuredBuffer<NwbRtInstanceMaterial, Std430DataLayout> g_NwbCausticSwInstanceMaterial;

// Per-mesh geometry (BVH nodes / raw positions / raw indices / per-corner attributes) is fetched from the bindless
// heap (step 4b) by the material record's per-buffer slots; the parallel per-mesh descriptor arrays that preceded it
// (bindings 5-8) were dropped in the step 4c bounded-path removal. The shared C++ backing buffers stay -- the heap
// descriptors point at them, and the per-dispatch buffer-state barriers still transition them for the heap reads.

// One caustic emission target (P1): the world-space AABB of a refractive instance. Mirrors the C++
// NwbCausticEmissionTargetGpu (32 bytes; the w lanes are unused padding).
struct NwbCausticEmissionTarget{
    float4 aabbMin;
    float4 aabbMax;
};

[[vk::binding(NWB_CAUSTIC_SW_BINDING_EMISSION_TARGETS, NWB_CAUSTIC_SW_SET)]]
StructuredBuffer<NwbCausticEmissionTarget, Std430DataLayout> g_NwbCausticSwEmissionTargets;

// Camera view buffer: only worldToClip is read (to project the receiver hit to a screen pixel for the splat).
// Std140 layout matching the C++ MeshViewGpuData / the mesh view_buffer.slangi struct.
struct NwbCausticSwView{
    row_major float4x4 worldToClip;
    row_major float4x4 clipToWorld;
    float4 cameraPosition;
    float4 frustumPlanes[NWB_MESH_VIEW_FRUSTUM_PLANE_COUNT];
};

[[vk::binding(NWB_CAUSTIC_SW_BINDING_VIEW, NWB_CAUSTIC_SW_SET)]]
ConstantBuffer<NwbCausticSwView, Std140DataLayout> g_NwbCausticSwView;

[[vk::binding(NWB_CAUSTIC_SW_BINDING_GBUFFER_DEPTH, NWB_CAUSTIC_SW_SET)]]
Texture2D<float4> g_NwbCausticSwGBufferDepth;

[[vk::binding(NWB_CAUSTIC_SW_BINDING_GBUFFER_WORLD_POSITION, NWB_CAUSTIC_SW_SET)]]
Texture2D<float4> g_NwbCausticSwGBufferWorldPosition;

[[vk::binding(NWB_CAUSTIC_SW_BINDING_ACCUMULATOR, NWB_CAUSTIC_SW_SET)]]
RWTexture2DArray<uint> g_NwbCausticSwAccumulator;

struct NwbCausticSwPushConstants{
    uint width;
    uint height;
    uint instanceCount;
    uint photonCount;
    uint emissionTargetCount;
    uint gridSide; // the FULL emission grid side (sqrt of the full photon budget); runtime so the density scales per build config
    uint frameIndex; // SW 2x temporal-reuse checkerboard phase: (frameIndex & 1) selects which half of the grid this frame emits
};

[[vk::push_constant]]
ConstantBuffer<NwbCausticSwPushConstants> g_NwbCausticSwPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float3 nwbCausticSwTransformPoint(const NwbCausticSwInstance instance, const float3 point){
    return float3(
        dot(instance.worldToObjectRow0.xyz, point) + instance.worldToObjectRow0.w,
        dot(instance.worldToObjectRow1.xyz, point) + instance.worldToObjectRow1.w,
        dot(instance.worldToObjectRow2.xyz, point) + instance.worldToObjectRow2.w
    );
}

// Direction transform (no translation), left UN-normalized so the ray parameter t is preserved between world and
// object space (object hit at t == world hit at t).
float3 nwbCausticSwTransformVector(const NwbCausticSwInstance instance, const float3 vector){
    return float3(
        dot(instance.worldToObjectRow0.xyz, vector),
        dot(instance.worldToObjectRow1.xyz, vector),
        dot(instance.worldToObjectRow2.xyz, vector)
    );
}

// Transforms an object-space normal/vector to world space with the inverse-transpose of object->world (the
// transpose of the world->object 3x3 carried in the instance rows), so a non-identity instance rotation/scale
// matches the rasterizer's world-space normal. Same transform the SW shadow trace's nwbSwShadowTransformNormal
// applies. Left UN-normalized (the shared loop orients + the caller normalizes for the refraction math).
float3 nwbCausticSwTransformNormal(const NwbCausticSwInstance instance, const float3 objectNormal){
    return float3(
        dot(float3(instance.worldToObjectRow0.x, instance.worldToObjectRow1.x, instance.worldToObjectRow2.x), objectNormal),
        dot(float3(instance.worldToObjectRow0.y, instance.worldToObjectRow1.y, instance.worldToObjectRow2.y), objectNormal),
        dot(float3(instance.worldToObjectRow0.z, instance.worldToObjectRow1.z, instance.worldToObjectRow2.z), objectNormal)
    );
}

struct NwbCausticSwTriangleHit{
    bool hit;
    float t;
    float u;
    float v;
    float w;
};

// Moller-Trumbore ray/triangle test (two-sided), reporting a hit strictly inside (tMin, tMax) with barycentrics.
NwbCausticSwTriangleHit nwbCausticSwRayTriangle(const float3 origin, const float3 direction, const float tMin, const float tMax, const float3 v0, const float3 v1, const float3 v2){
    NwbCausticSwTriangleHit result;
    result.hit = false;
    result.t = 0.0;
    result.u = 0.0;
    result.v = 0.0;
    result.w = 0.0;

    const NwbRayTriangleHit hit = nwbRayTriangleMollerTrumbore(origin, direction, tMin, tMax, v0, v1, v2);
    result.hit = hit.hit;
    result.t = hit.t;
    result.u = hit.u;
    result.v = hit.v;
    result.w = hit.w;
    return result;
}

struct NwbCausticSwVertexAttrib{
    float3 normal;
    float2 uv0;
};

NwbCausticSwVertexAttrib nwbCausticSwLoadVertexAttrib(const uint attributeSlot, const uint attributeIndex){
    // attributeSlot is this mesh's per-corner attribute buffer's GLOBAL HEAP slot (carried on the instance material
    // record, populated lockstep with the mesh in rt_swbvh.cpp). It is non-uniform across the wave -- each photon's ray
    // hits a different mesh -- so the heap subscript MUST be NonUniformResourceIndex-tagged; NwbHeapRawBuffer() applies
    // that internally and is re-invoked at each Load (mirroring the bounded g_NwbCausticSwMeshAttributes[
    // NonUniformResourceIndex(meshIndex)] it replaces) so the decoration is applied fresh at every descriptor access.
    // One Load4 for the whole 16-byte attribute record (packed normal in .xy, uv0 in .zw) -- a single memory request
    // instead of two dependent Load2s (bit-identical result, lower latency in the refractive-hit path).
    const uint4 packed = NwbHeapRawBuffer(attributeSlot).Load4(attributeIndex * 16u);
    NwbCausticSwVertexAttrib attrib;
    attrib.normal = float3(
        nwbUnpackHalf2x16(packed.x),
        nwbUnpackHalf2x16(packed.y).x
    );
    attrib.uv0 = asfloat(packed.zw);
    return attrib;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Per-instance closest-hit refinement: push the world ray into the instance's object space, walk that mesh's
// triangle BVH, and keep the NEAREST triangle crossing (smallest t). Updates the running closest hit in `closest`
// (its .t is the current min). On a closer hit it records the world-space hit point, the interpolated SHADING world
// normal (a0*w + a1*u + a2*v transformed by the inverse-transpose -- smooth across the mesh -> smooth caustic; the
// HW closest-hit interpolates the SAME way for parity), the refractive flag, and -- for a refractive crossing -- the
// optical params (ior / transmission) from the occluder's surface hook, evaluated through the SAME dispatch the
// shadow trace uses.
void nwbCausticSwInstanceClosest(const uint instanceIndex, const float3 worldOrigin, const float3 worldDirection, const float tMin, inout NwbCausticHit closest){
    const NwbCausticSwInstance instance = g_NwbCausticSwInstances[instanceIndex];
    const NwbRtInstanceMaterial material = g_NwbCausticSwInstanceMaterial[instanceIndex];
    const bool refractive = (material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE) != 0u;
    // This instance's per-mesh geometry (BVH nodes / indices / positions / attributes) is fetched from the global
    // descriptor heap by the four per-buffer slots the material record carries (populated lockstep with the mesh -- and
    // with instance.meshIndex -- in rt_swbvh.cpp). Each slot is NON-UNIFORM across the wave -- different photons' rays
    // hit different meshes -- so its heap subscript must be NonUniformResourceIndex-tagged; NwbHeapBvhNodes() /
    // NwbHeapRawBuffer() apply that internally and are re-invoked at EACH access below (never hoisted into a resource
    // local) so the decoration is applied fresh at every subscript, exactly as the bounded
    // g_NwbCausticSwMesh*[NonUniformResourceIndex(meshIndex)] arrays they replace did. Omitting it is undefined
    // behaviour: a wave straddling a mesh boundary reads a DIFFERENT mesh's BVH/geometry for some lanes.
    const float3 origin = nwbCausticSwTransformPoint(instance, worldOrigin);
    const float3 direction = nwbCausticSwTransformVector(instance, worldDirection);
    const float3 invDir = nwbBvhSafeInvDir(direction);

    // FRONT-TO-BACK ordered descent. Keep the current node loaded; at an internal node descend the NEARER-entry child
    // and defer the farther onto the stack (re-tested when popped, against the by-then-smaller closest.t). Processing
    // near first shrinks closest.t sooner, so more far subtrees cull -> fewer triangle tests + refractive dispatches.
    // The stack holds only deferred FAR children (<= tree depth).
    uint stack[NWB_CAUSTIC_SW_MESH_STACK_SIZE];
    uint stackPointer = 0u;
    NwbBvhNode node = NwbHeapBvhNodes(material.nodeSlot)[0u]; // per-mesh root
    bool active = nwbBvhRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax, 0.0);

    [loop] while(active){
        if(nwbBvhNodeIsLeaf(node)){
            const uint triangle = nwbBvhLeafPrimitive(node);
            const uint indexBase = triangle * 3u;
            // The 3 triangle indices are contiguous -> one Load3 instead of three Loads (bit-identical, one request).
            const uint3 triIndices = NwbHeapRawBuffer(material.indexSlot).Load3(indexBase * 4u);
            const uint i0 = triIndices.x;
            const uint i1 = triIndices.y;
            const uint i2 = triIndices.z;
            const float3 v0 = asfloat(NwbHeapRawBuffer(material.positionSlot).Load3(i0 * 12u));
            const float3 v1 = asfloat(NwbHeapRawBuffer(material.positionSlot).Load3(i1 * 12u));
            const float3 v2 = asfloat(NwbHeapRawBuffer(material.positionSlot).Load3(i2 * 12u));
            const NwbCausticSwTriangleHit triangleHit = nwbCausticSwRayTriangle(origin, direction, tMin, closest.t, v0, v1, v2);
            if(triangleHit.hit){
                // Closer than the running min -> adopt it.
                closest.hit = true;
                closest.t = triangleHit.t;
                closest.isRefractive = refractive;
                closest.worldPos = worldOrigin + worldDirection * triangleHit.t;
                closest.shadingNormal = float3(0.0, 0.0, 1.0); // filled with the real interpolated normal for a refractive hit below
                closest.ior = 1.0;
                closest.transmission = half3(1.0, 1.0, 1.0);

                if(refractive){
                    // Evaluate this material's surface hook for its optical params, the SAME way the shadow trace
                    // does (interpolated normal/uv0 + the material-constants context the instance record carries).
                    const uint attributeBase = triangle * 3u;
                    const NwbCausticSwVertexAttrib a0 = nwbCausticSwLoadVertexAttrib(material.attributeSlot, attributeBase);
                    const NwbCausticSwVertexAttrib a1 = nwbCausticSwLoadVertexAttrib(material.attributeSlot, attributeBase + 1u);
                    const NwbCausticSwVertexAttrib a2 = nwbCausticSwLoadVertexAttrib(material.attributeSlot, attributeBase + 2u);
                    const float3 objectShadingNormal = a0.normal * triangleHit.w + a1.normal * triangleHit.u + a2.normal * triangleHit.v;

                    // The interpolated SHADING normal in world space (inverse-transpose of object->world). This is the
                    // bend normal (smooth across the mesh -> smooth caustic) AND the surface-dispatch normal.
                    const float3 worldShadingNormal = nwbSafeNormalize(nwbCausticSwTransformNormal(instance, objectShadingNormal), float3(0.0, 0.0, 1.0));
                    closest.shadingNormal = worldShadingNormal;

                    NwbShadowHit hit;
                    hit.meshInstanceIndex = material.meshInstanceIndex;
                    hit.shadowTransmittanceModelId = material.shadowTransmittanceModelId;
                    hit.materialConstantByteOffset = material.materialConstantByteOffset;
                    hit.uv0 = a0.uv0 * triangleHit.w + a1.uv0 * triangleHit.u + a2.uv0 * triangleHit.v;
                    hit.worldNormal = worldShadingNormal;
                    hit.worldPosition = closest.worldPos;
                    hit.rayDirection = worldDirection;

                    const NwbMeshSurface surface = nwbShadowDispatchSurface(material.shadowTransmittanceModelId, hit);
                    closest.ior = surface.refractionIor;
                    closest.transmission = surface.shadowAbsorptionTint;
                }
            }
            active = false; // leaf done -> pop the next deferred node below
        }
        else{
            const NwbBvhNode leftNode = NwbHeapBvhNodes(material.nodeSlot)[node.leftChild];
            const NwbBvhNode rightNode = NwbHeapBvhNodes(material.nodeSlot)[node.rightChild];
            float tLeft;
            const bool hitLeft = nwbBvhRayAabbT(origin, invDir, tMin, closest.t, leftNode.aabbMin, leftNode.aabbMax, 0.0, tLeft);
            float tRight;
            const bool hitRight = nwbBvhRayAabbT(origin, invDir, tMin, closest.t, rightNode.aabbMin, rightNode.aabbMax, 0.0, tRight);
            if(hitLeft && hitRight){
                // Descend the nearer child; defer the farther. A full stack drops that farther subtree rather than
                // corrupting traversal state.
                if(tLeft <= tRight){
                    if(stackPointer < NWB_CAUSTIC_SW_MESH_STACK_SIZE) stack[stackPointer++] = node.rightChild;
                    node = leftNode;
                }
                else{
                    if(stackPointer < NWB_CAUSTIC_SW_MESH_STACK_SIZE) stack[stackPointer++] = node.leftChild;
                    node = rightNode;
                }
                continue;
            }
            if(hitLeft){ node = leftNode; continue; }
            if(hitRight){ node = rightNode; continue; }
            active = false; // both children culled -> pop
        }

        // Pop the next deferred node that still survives the (now-smaller) closest.t cull; drain -> done.
        [loop] while(stackPointer > 0u){
            node = NwbHeapBvhNodes(material.nodeSlot)[stack[--stackPointer]];
            if(nwbBvhRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax, 0.0)){
                active = true;
                break;
            }
        }
    }
}

// The SW backend's closest-hit hook: walk the software scene/instance BVH for every instance whose world AABB the
// ray crosses, refining each with the exact per-mesh triangle traversal and keeping the nearest hit across all of
// them. Mirrors nwbSwShadowOccluded's scene-BVH descent but tracks min-t instead of accumulating.
NwbCausticHit nwbCausticTraceClosest(float3 origin, float3 direction, float tMin, float tMax){
    NwbCausticHit closest;
    closest.hit = false;
    closest.isRefractive = false;
    closest.t = tMax;
    closest.ior = 1.0;
    closest.worldPos = float3(0.0, 0.0, 0.0);
    closest.shadingNormal = float3(0.0, 0.0, 1.0);
    closest.transmission = half3(1.0, 1.0, 1.0);

    if(g_NwbCausticSwPushConstants.instanceCount == 0u)
        return closest;

    const float3 invDir = nwbBvhSafeInvDir(direction);

    // FRONT-TO-BACK ordered descent (same scheme as the per-mesh traversal). Here a "leaf" is an INSTANCE whose whole
    // mesh traversal (+ surface dispatches) is expensive, and the demo's refractors OVERLAP, so processing the nearer
    // instance first shrinks closest.t and culls the farther instances' AABBs -> skips entire mesh traversals. Output-
    // identical (closest-hit is order-independent).
    uint stack[NWB_CAUSTIC_SW_SCENE_STACK_SIZE];
    uint stackPointer = 0u;
    NwbBvhNode node = g_NwbCausticSwSceneNodes[0u]; // root
    bool active = nwbBvhRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax, 0.0);

    [loop] while(active){
        if(nwbBvhNodeIsLeaf(node)){
            nwbCausticSwInstanceClosest(nwbBvhLeafPrimitive(node), origin, direction, tMin, closest);
            active = false; // -> pop below
        }
        else{
            const NwbBvhNode leftNode = g_NwbCausticSwSceneNodes[node.leftChild];
            const NwbBvhNode rightNode = g_NwbCausticSwSceneNodes[node.rightChild];
            float tLeft;
            const bool hitLeft = nwbBvhRayAabbT(origin, invDir, tMin, closest.t, leftNode.aabbMin, leftNode.aabbMax, 0.0, tLeft);
            float tRight;
            const bool hitRight = nwbBvhRayAabbT(origin, invDir, tMin, closest.t, rightNode.aabbMin, rightNode.aabbMax, 0.0, tRight);
            if(hitLeft && hitRight){
                if(tLeft <= tRight){
                    if(stackPointer < NWB_CAUSTIC_SW_SCENE_STACK_SIZE) stack[stackPointer++] = node.rightChild;
                    node = leftNode;
                }
                else{
                    if(stackPointer < NWB_CAUSTIC_SW_SCENE_STACK_SIZE) stack[stackPointer++] = node.leftChild;
                    node = rightNode;
                }
                continue;
            }
            if(hitLeft){ node = leftNode; continue; }
            if(hitRight){ node = rightNode; continue; }
            active = false; // both children culled -> pop
        }

        [loop] while(stackPointer > 0u){
            node = g_NwbCausticSwSceneNodes[stack[--stackPointer]];
            if(nwbBvhRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax, 0.0)){
                active = true;
                break;
            }
        }
    }

    return closest;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CAUSTIC_SPLAT_VIEW g_NwbCausticSwView
#define NWB_CAUSTIC_SPLAT_PUSH g_NwbCausticSwPushConstants
#define NWB_CAUSTIC_SPLAT_GBUFFER_DEPTH g_NwbCausticSwGBufferDepth
#define NWB_CAUSTIC_SPLAT_GBUFFER_WORLD_POSITION g_NwbCausticSwGBufferWorldPosition
#define NWB_CAUSTIC_SPLAT_ACCUMULATOR g_NwbCausticSwAccumulator
#include "splat.slangi"
#undef NWB_CAUSTIC_SPLAT_ACCUMULATOR
#undef NWB_CAUSTIC_SPLAT_GBUFFER_WORLD_POSITION
#undef NWB_CAUSTIC_SPLAT_GBUFFER_DEPTH
#undef NWB_CAUSTIC_SPLAT_PUSH
#undef NWB_CAUSTIC_SPLAT_VIEW


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CAUSTIC_SW_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint photonIndex = dispatchThreadID.x;
    if(photonIndex >= g_NwbCausticSwPushConstants.photonCount)
        return;
    if(g_NwbCausticSwPushConstants.emissionTargetCount == 0u || g_NwbCausticSwPushConstants.instanceCount == 0u)
        return;

    NwbSceneLight light;
    if(!nwbCausticPickLight(photonIndex, light))
        return;

    // The light's principal travel direction (FROM the light). NwbSceneLight.direction is the toward-light /
    // emission axis; the photon travels along the emission axis. For a directional/spot caustic light params.y is
    // 0 (directional) or 2 (spot); point lights are excluded upstream (no caustic slot assigned).
    const float3 lightDir = nwbSafeNormalize(-light.direction.xyz, float3(0.0, 0.0, -1.0));

    // Per-target emission (the make-or-break aim): assign each photon to ONE refractive target and aim it at that
    // target's world AABB. Aiming at the COMBINED box over the spread tetrahedra centres the domain in the GAP between
    // them and wastes most photons on empty space; per-target keeps the domain tight on actual glass so a photon is
    // guaranteed to pass through (or graze) its target. The target is picked by a HASH of photonIndex, NOT
    // photonIndex % count: the cell (column,row) below is also photonIndex-derived, so a plain modulo correlates the
    // target with the grid -- e.g. with 3 targets on a 512-wide grid (512 == 2 mod 3) each target gets the cells where
    // col == row + k (mod 3), a DIAGONAL-STRIPED 1/3 subset that the refractor images into diagonal caustic streaks
    // shooting far across the receiver (invisible when sparse, glaring once dense). The hash decorrelates target from
    // cell, so each target gets a uniformly-scattered subset of the stratified grid -> no stripes, caustic stays put.
    const uint targetIndex = nwbCausticHashU32(photonIndex) % g_NwbCausticSwPushConstants.emissionTargetCount;
    const float3 targetAabbMin = g_NwbCausticSwEmissionTargets[targetIndex].aabbMin.xyz;
    const float3 targetAabbMax = g_NwbCausticSwEmissionTargets[targetIndex].aabbMax.xyz;

    const float2 cell = nwbCausticJitteredCheckerboardCell(
        photonIndex,
        g_NwbCausticSwPushConstants.gridSide,
        g_NwbCausticSwPushConstants.frameIndex
    );

    const NwbCausticEmission emission = nwbCausticEmitPhotonAtTarget(lightDir, targetAabbMin, targetAabbMax, cell);
    if(!emission.valid)
        return;

    const NwbCausticPhoton photon = nwbCausticCreatePhoton(
        emission,
        nwbCausticLightColor(light.colorIntensity),
        g_NwbCausticSwPushConstants.emissionTargetCount,
        g_NwbCausticSwPushConstants.photonCount
    );

    const NwbCausticSplat splat = nwbCausticTracePhoton(photon);
    if(!splat.valid)
        return;

    nwbCausticSplat(splat.worldPos, splat.flux);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

