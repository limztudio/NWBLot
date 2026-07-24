// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_raytracing_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Extracts the axis-th component (0=x, 1=y, 2=z) of a SIMD vector.
[[nodiscard]] f32 SceneBvhAxisComponent(const SIMDVector value, const u32 axis)noexcept{
    return axis == 0u ? VectorGetX(value) : (axis == 1u ? VectorGetY(value) : VectorGetZ(value));
}

// Surface area of an AABB from its min/max SIMD corners (the SA term in the binned-SAH split cost).
[[nodiscard]] f32 SceneBvhAabbSurfaceArea(const SIMDVector aabbMin, const SIMDVector aabbMax)noexcept{
    const SIMDVector extent = VectorSubtract(aabbMax, aabbMin);
    const SIMDVector pairProducts = VectorMultiply(
        extent,
        VectorSwizzle<1, 2, 0, 3>(extent)
    );
    const SIMDVector area = VectorScale(
        VectorAdd(
            VectorAdd(VectorSplatX(pairProducts), VectorSplatZ(pairProducts)),
            VectorSplatY(pairProducts)
        ),
        2.0f
    );
    return VectorGetX(area);
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

// Composes the affine object->world matrix from already-loaded scale/rotation/translation SIMD vectors.
// The SIMD<->storage boundary stays in the caller: this helper only does SIMD math.
SIMDMatrix BuildObjectToWorld(const SIMDVector scale, const SIMDVector rotation, const SIMDVector translation)noexcept{
    return MatrixAffineTransformation(
        scale,
        VectorZero(),
        rotation,
        translation
    );
}

bool ResolveRenderableMeshResources(
    MeshSystem& meshSystem,
    RendererMeshSystem& rendererMeshSystem,
    const Core::ECS::EntityID entity,
    RenderableMeshDesc& outResolvedMesh,
    MeshResources*& outMesh
){
    outMesh = nullptr;
    if(!meshSystem.resolveRenderableMesh(entity, outResolvedMesh))
        return false;

    return outResolvedMesh.runtime
        ? rendererMeshSystem.findRuntimeMeshResources(outResolvedMesh.runtimeMesh, outMesh)
        : rendererMeshSystem.findMeshResources(outResolvedMesh.mesh, outMesh)
    ;
}

// Recursively builds a binary BVH over the [lo, hi) slice of the instance-index permutation, appending nodes
// to `nodes` (the first node appended for the whole range is the root, index 0). Each internal node splits on
// the axis + bin boundary of lowest binned-SAH cost: it sweeps a fixed-grid binning of all three centroid axes,
// accumulating per-bin AABBs, instance counts, and leaf-cost sums, then evaluates the classic
// cost = ct + (SA_L*cost_L + SA_R*cost_R)/SA_parent at every boundary and takes the minimum. When no axis has a
// separating bin boundary (coincident centroids everywhere) it falls back to the count median, so the build never
// produces an empty/degenerate split. Leaves store NWB_BVH_LEAF_FLAG | instanceIndex + the instance world AABB;
// internal nodes store child node indices + the unioned box — the exact NwbBvhNode layout the per-mesh build
// produces, so the GPU traversal is uniform across the scene BVH and every per-mesh BVH. `instanceLeafCost`
// weights each instance's leaf cost; in production this is the instance triangle count so a large instance biases
// the tree like a large primitive would. When null, every instance counts uniformly (the self-test path).
u32 BuildSceneBvhNode(
    u32* indices,
    const u32 lo,
    const u32 hi,
    const Float4* instanceAabbMin,
    const Float4* instanceAabbMax,
    const Float4* instanceCentroid,
    Vector<SceneBvhNodeBuildData, Core::Alloc::ScratchArena>& nodes,
    const u32* instanceLeafCost
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

    // Per-instance leaf cost: primitive count in production, uniform (1) in the self-test path. Uniform cost is
    // the classic TLAS model where every instance intersection costs the same; the production weight is what makes
    // SAH at TLAS scale meaningful (a 100k-triangle instance should bias the tree like a large primitive).
    auto leafCostOf = [instanceLeafCost](const u32 instance) -> f32 {
        return instanceLeafCost ? static_cast<f32>(instanceLeafCost[instance]) : 1.0f;
    };

    // Bounding centroid extent over the slice: the per-axis extent picks which axes are binable (a zero extent
    // axis cannot separate any instances and is skipped).
    SIMDVector centroidMin = VectorReplicate(s_RayTracingFiniteInfinity);
    SIMDVector centroidMax = VectorReplicate(-s_RayTracingFiniteInfinity);
    for(u32 i = lo; i < hi; ++i){
        const SIMDVector centroid = LoadFloat(instanceCentroid[indices[i]]);
        centroidMin = VectorMin(centroidMin, centroid);
        centroidMax = VectorMax(centroidMax, centroid);
    }

    // Per-bin accumulators for a single axis: AABB min/max, instance count, summed leaf cost. s_SceneBvhSahBinCount
    // bins is the standard binned-BVH granularity and keeps the bin grid cache-resident.
    SIMDVector binMin[s_SceneBvhSahBinCount];
    SIMDVector binMax[s_SceneBvhSahBinCount];
    f32 binCost[s_SceneBvhSahBinCount];
    u32 binCount[s_SceneBvhSahBinCount];

    bool anyAxisValid = false;
    u32 bestAxis = 0u;
    u32 bestBin = 0u;
    f32 bestCost = s_RayTracingFiniteInfinity;

    for(u32 axis = 0u; axis < s_SceneBvhAxisCount; ++axis){
        const f32 loExtent = SceneBvhAxisComponent(centroidMin, axis);
        const f32 hiExtent = SceneBvhAxisComponent(centroidMax, axis);
        const f32 extent = hiExtent - loExtent;
        if(extent <= 0.0f)
            continue; // coincident centroids on this axis -> binning cannot split it
        anyAxisValid = true;
        const f32 invExtent = s_SceneBvhSahBinCount / extent;

        for(u32 b = 0u; b < s_SceneBvhSahBinCount; ++b){
            binMin[b] = VectorReplicate(s_RayTracingFiniteInfinity);
            binMax[b] = VectorReplicate(-s_RayTracingFiniteInfinity);
            binCost[b] = 0.0f;
            binCount[b] = 0u;
        }
        for(u32 i = lo; i < hi; ++i){
            const u32 instance = indices[i];
            const f32 c = SceneBvhAxisComponent(LoadFloat(instanceCentroid[instance]), axis);
            u32 b = static_cast<u32>((c - loExtent) * invExtent);
            if(b >= s_SceneBvhSahBinCount)
                b = s_SceneBvhSahBinCount - 1u;
            binMin[b] = VectorMin(binMin[b], LoadFloat(instanceAabbMin[instance]));
            binMax[b] = VectorMax(binMax[b], LoadFloat(instanceAabbMax[instance]));
            binCost[b] += leafCostOf(instance);
            ++binCount[b];
        }

        // Right-to-left suffix AABB / count / cost so every boundary's right partition is exact, then sweep
        // boundaries left-to-right building the matching left prefix and evaluate the SAH at each one.
        SIMDVector suffixMin[s_SceneBvhSahBinCount];
        SIMDVector suffixMax[s_SceneBvhSahBinCount];
        f32 suffixCost[s_SceneBvhSahBinCount];
        u32 suffixCount[s_SceneBvhSahBinCount];
        {
            SIMDVector accMin = VectorReplicate(s_RayTracingFiniteInfinity);
            SIMDVector accMax = VectorReplicate(-s_RayTracingFiniteInfinity);
            f32 accCost = 0.0f;
            u32 accCount = 0u;
            for(u32 b = s_SceneBvhSahBinCount; b >= 1u; --b){
                const u32 idx = b - 1u;
                accMin = VectorMin(accMin, binMin[idx]);
                accMax = VectorMax(accMax, binMax[idx]);
                accCost += binCost[idx];
                accCount += binCount[idx];
                suffixMin[idx] = accMin;
                suffixMax[idx] = accMax;
                suffixCost[idx] = accCost;
                suffixCount[idx] = accCount;
            }
        }

        // Boundary k splits at bin k: left = bins [0, k), right = bins [k, end). The parent AABB is the union of
        // every bin (suffix at index 0); the SAH parent-area divisor is folded out by comparing costs on the same
        // slice, so the un-normalized cost ct*SA + SA_L*cost_L + SA_R*cost_R is minimized directly.
        const SIMDVector parentMin = suffixMin[0u];
        const SIMDVector parentMax = suffixMax[0u];
        const f32 parentArea = SceneBvhAabbSurfaceArea(parentMin, parentMax);

        SIMDVector leftMin = VectorReplicate(s_RayTracingFiniteInfinity);
        SIMDVector leftMax = VectorReplicate(-s_RayTracingFiniteInfinity);
        f32 leftCost = 0.0f;
        u32 leftCount = 0u;
        for(u32 k = 1u; k < s_SceneBvhSahBinCount; ++k){
            const u32 b = k - 1u;
            leftMin = VectorMin(leftMin, binMin[b]);
            leftMax = VectorMax(leftMax, binMax[b]);
            leftCost += binCost[b];
            leftCount += binCount[b];

            const u32 rightCount = suffixCount[k];
            if(leftCount == 0u || rightCount == 0u)
                continue; // boundary puts every instance on one side -> not a real split

            const f32 leftArea = SceneBvhAabbSurfaceArea(leftMin, leftMax);
            const f32 rightArea = SceneBvhAabbSurfaceArea(suffixMin[k], suffixMax[k]);
            const f32 cost = parentArea * s_SceneBvhSahTraversalCost
                + leftArea * leftCost
                + rightArea * suffixCost[k];
            if(cost < bestCost){
                bestCost = cost;
                bestAxis = axis;
                bestBin = k;
            }
        }
    }

    u32 mid;
    if(anyAxisValid){
        // Partition the index slice by the winning axis/bin boundary, matching the binning that produced bestCost.
        const f32 loExtent = SceneBvhAxisComponent(centroidMin, bestAxis);
        const f32 hiExtent = SceneBvhAxisComponent(centroidMax, bestAxis);
        const f32 extent = hiExtent - loExtent;
        const f32 invExtent = s_SceneBvhSahBinCount / extent;
        mid = lo;
        for(u32 i = lo; i < hi; ++i){
            const u32 instance = indices[i];
            const f32 c = SceneBvhAxisComponent(LoadFloat(instanceCentroid[instance]), bestAxis);
            u32 b = static_cast<u32>((c - loExtent) * invExtent);
            if(b >= s_SceneBvhSahBinCount)
                b = s_SceneBvhSahBinCount - 1u;
            if(b < bestBin){
                const u32 swap = indices[i];
                indices[i] = indices[mid];
                indices[mid] = swap;
                ++mid;
            }
        }
    }

    if(!anyAxisValid || mid == lo || mid == hi)
        mid = lo + (hi - lo) / 2u; // degenerate split (coincident centroids everywhere) -> count median; still correct

    const u32 leftChild = BuildSceneBvhNode(indices, lo, mid, instanceAabbMin, instanceAabbMax, instanceCentroid, nodes, instanceLeafCost);
    const u32 rightChild = BuildSceneBvhNode(indices, mid, hi, instanceAabbMin, instanceAabbMax, instanceCentroid, nodes, instanceLeafCost);

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

