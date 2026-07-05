// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "rt_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_raytracing_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Extracts the axis-th component (0=x, 1=y, 2=z) of a SIMD vector.
[[nodiscard]] f32 SceneBvhAxisComponent(const SIMDVector value, const u32 axis)noexcept{
    return axis == 0u ? VectorGetX(value) : (axis == 1u ? VectorGetY(value) : VectorGetZ(value));
}

void InflateSwShadowSceneBounds(SIMDVector& boundsMin, SIMDVector& boundsMax)noexcept{
    const SIMDVector extent = VectorSubtract(boundsMax, boundsMin);
    const SIMDVector paddingVector = VectorSetW(
        VectorMax(
            VectorReplicate(s_SwShadowSceneBoundsMinPadding),
            VectorScale(Vector3MaxComponent(extent), s_SwShadowSceneBoundsRelativePadding)
        ),
        0.0f
    );
    boundsMin = VectorSubtract(boundsMin, paddingVector);
    boundsMax = VectorAdd(boundsMax, paddingVector);
}

// Recursively builds a binary BVH over the [lo, hi) slice of the instance-index permutation, appending nodes
// to `nodes` (the first node appended for the whole range is the root, index 0). It splits at the spatial
// median of the largest centroid-extent axis, falling back to the count median when a spatial split puts
// every instance on one side. Leaves store NWB_BVH_LEAF_FLAG | instanceIndex + the instance world AABB;
// internal nodes store child node indices + the unioned box — the exact NwbBvhNode layout the per-mesh build
// produces, so the GPU traversal is uniform across the scene BVH and every per-mesh BVH.
u32 BuildSceneBvhNode(
    u32* indices,
    const u32 lo,
    const u32 hi,
    const Float4* instanceAabbMin,
    const Float4* instanceAabbMax,
    const Float4* instanceCentroid,
    Vector<SceneBvhNodeBuildData, Core::Alloc::ScratchArena>& nodes
){
    const u32 nodeIndex = static_cast<u32>(nodes.size());
    nodes.push_back(SceneBvhNodeBuildData{});

    if((hi - lo) == 1u){
        const u32 instance = indices[lo];
        SceneBvhNodeBuildData& node = nodes[nodeIndex];
        node.aabbMin = instanceAabbMin[instance];
        node.aabbMax = instanceAabbMax[instance];
        node.leftChild = BvhNodeIndex::LeafFlag | instance;
        node.rightChild = 1u;
        return nodeIndex;
    }

    SIMDVector centroidMin = VectorReplicate(1e30f);
    SIMDVector centroidMax = VectorReplicate(-1e30f);
    for(u32 i = lo; i < hi; ++i){
        const SIMDVector centroid = LoadFloat(instanceCentroid[indices[i]]);
        centroidMin = VectorMin(centroidMin, centroid);
        centroidMax = VectorMax(centroidMax, centroid);
    }

    const SIMDVector centroidExtent = VectorSubtract(centroidMax, centroidMin);
    u32 axis = 0u;
    f32 axisExtent = VectorGetX(centroidExtent);
    if(VectorGetY(centroidExtent) > axisExtent){ axis = 1u; axisExtent = VectorGetY(centroidExtent); }
    if(VectorGetZ(centroidExtent) > axisExtent){ axis = 2u; }

    const f32 splitValue = 0.5f * (SceneBvhAxisComponent(centroidMin, axis) + SceneBvhAxisComponent(centroidMax, axis));

    u32 mid = lo;
    for(u32 i = lo; i < hi; ++i){
        if(SceneBvhAxisComponent(LoadFloat(instanceCentroid[indices[i]]), axis) < splitValue){
            const u32 swap = indices[i];
            indices[i] = indices[mid];
            indices[mid] = swap;
            ++mid;
        }
    }
    if(mid == lo || mid == hi)
        mid = lo + (hi - lo) / 2u; // degenerate spatial split (coincident centroids) -> count median; still correct

    const u32 leftChild = BuildSceneBvhNode(indices, lo, mid, instanceAabbMin, instanceAabbMax, instanceCentroid, nodes);
    const u32 rightChild = BuildSceneBvhNode(indices, mid, hi, instanceAabbMin, instanceAabbMax, instanceCentroid, nodes);

    SceneBvhNodeBuildData& node = nodes[nodeIndex];
    StoreFloat(VectorMin(LoadFloat(nodes[leftChild].aabbMin), LoadFloat(nodes[rightChild].aabbMin)), &node.aabbMin);
    StoreFloat(VectorMax(LoadFloat(nodes[leftChild].aabbMax), LoadFloat(nodes[rightChild].aabbMax)), &node.aabbMax);
    node.leftChild = leftChild;
    node.rightChild = rightChild;
    return nodeIndex;
}

// Builds the per-instance shadow-occluder material record from the cooked material surface info: the
// transmittance-model id (dispatches the per-hit transmittance hook) + the transparent flag. Both shadow builders
// append exactly one of these per instance, in instance push order, so the table indexes by shadow instance id.
// The per-mesh attribute slot + the material-constants context (constant byte offset + g_NwbMeshInstances index)
// are supplied by the caller, which resolves them alongside the occluder's mesh + per-entity instance mapping.
[[nodiscard]] NwbRtInstanceMaterialGpu ResolveInstanceShadowMaterial(
    const MaterialSurfaceInfo& materialInfo,
    const u32 meshSlot,
    const u32 materialConstantByteOffset,
    const u32 meshInstanceIndex
){
    NwbRtInstanceMaterialGpu material;
    material.shadowTransmittanceModelId = materialInfo.shadowTransmittanceModelId;
    material.flags =
        (materialInfo.transparent ? RtInstanceMaterialFlag::Transparent : RtInstanceMaterialFlag::None)
        | (materialInfo.refractive ? RtInstanceMaterialFlag::Refractive : RtInstanceMaterialFlag::None);
    material.meshSlot = meshSlot;
    material.materialConstantByteOffset = materialConstantByteOffset;
    material.meshInstanceIndex = meshInstanceIndex;
    return material;
}

// Bit-cast a float to its u32 representation (the software producers store base colour as asuint in the u32-typed
// instance-material fields; the shader reads them back with asfloat).
[[nodiscard]] u32 FloatToUintBits(const f32 value){
    u32 bits = 0u;
    NWB_MEMCPY(&bits, sizeof(bits), &value, sizeof(value));
    return bits;
}

// Resolves + writes the per-instance base colour the software probe/photon producers shade bounces with: the entity's
// authored per-instance tint (mutable "runtime.color_tint", which the scenes set and which flows to the G-buffer base
// colour), else the neutral GI default. Always runs (even for an unresolved material) so a bounce is never black.
void AssignInstanceBaseColor(NwbRtInstanceMaterialGpu& material, Core::ECS::World& world, const Core::ECS::EntityID entity){
    Float4 color(NWB_SURFEL_DEFAULT_ALBEDO_FLOAT3, 1.0f);
    if(const MaterialInstanceComponent* materialInstance = world.tryGetComponent<MaterialInstanceComponent>(entity)){
        Float4 tint;
        if(GetMaterialMutableHalf4(*materialInstance, "runtime.color_tint", tint))
            color = tint;
    }
    material.baseColorR = FloatToUintBits(color.x);
    material.baseColorG = FloatToUintBits(color.y);
    material.baseColorB = FloatToUintBits(color.z);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

