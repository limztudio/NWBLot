// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void InflateSwShadowSceneBounds(SIMDVectorSingle& boundsMin, SIMDVectorSingle& boundsMax)noexcept{
    const SIMDVectorSingle extent = VectorSubtract(boundsMax, boundsMin);
    const SIMDVectorSingle paddingVector = VectorSetW(VectorMax(VectorReplicate(s_SwShadowSceneBoundsMinPadding), VectorScale(Vector3MaxComponent(extent), s_SwShadowSceneBoundsRelativePadding)), 0.0f);
    boundsMin = VectorSubtract(boundsMin, paddingVector);
    boundsMax = VectorAdd(boundsMax, paddingVector);
}

Expected<RenderableRayTracingMeshResources, RenderableMeshResolution::Enum> ResolveRenderableMeshResources(MeshSystem& meshSystem, RendererMeshSystem& rendererMeshSystem, const Core::ECS::EntityID entity){
    auto resolvedMesh = meshSystem.resolveRenderableMeshStatus(entity);
    if(!resolvedMesh)
        return MakeUnexpected(resolvedMesh.error());
    auto mesh = rendererMeshSystem.findRenderableRayTracingResourceSnapshot(*resolvedMesh);
    if(!mesh)
        return MakeUnexpected(RenderableMeshResolution::Unavailable);
    return RenderableRayTracingMeshResources{ Move(*resolvedMesh), Move(*mesh) };
}

[[nodiscard]] b8 IsHeapHandle(const Core::GpuDescriptorHandle handle, const Core::GpuDescriptorClass::Enum descriptorClass)noexcept{
    return handle.valid() && handle.descriptorClass() == descriptorClass;
}

[[nodiscard]] Expected<Core::GpuDescriptorHandle> RegisterHeapBuffer(Core::GpuDescriptorHeap& heap, Core::Buffer& buffer, const Core::GpuDescriptorClass::Enum descriptorClass, const b8 writable){
    const Expected<Core::GpuDescriptorHandle> allocated = heap.allocate(descriptorClass);
    if(!allocated)
        return MakeUnexpected(allocated.error());
    const Core::GpuDescriptorHandle handle = *allocated;

    Core::GpuDescriptorRetirementGuard retireUnpublished(heap, handle);

    const Core::DescriptorWriteItem item = descriptorClass == Core::GpuDescriptorClass::UniformBuffer
        ? Core::DescriptorWriteItem::ConstantBuffer(0u, &buffer)
        : (writable ? Core::DescriptorWriteItem::StructuredBufferUav(0u, &buffer) : Core::DescriptorWriteItem::StructuredBufferSrv(0u, &buffer))
    ;
    if(!heap.write(handle, item)){
        return MakeUnexpected(Failure{});
    }
    retireUnpublished.release();
    return handle;
}

[[nodiscard]] b8 EnsureHeapBuffer(const Core::GpuDescriptorClass::Enum descriptorClass, const b8 writable, Core::GpuDescriptorHeap& heap, Core::Buffer& buffer, Core::GpuDescriptorHandle& inOutHandle){
    if(inOutHandle.valid())
        return IsHeapHandle(inOutHandle, descriptorClass);

    const auto acquired = RegisterHeapBuffer(heap, buffer, descriptorClass, writable);
    if(!acquired)
        return false;
    inOutHandle = *acquired;
    return true;
}

[[nodiscard]] b8 ReplaceHeapBuffer(const Core::GpuDescriptorClass::Enum descriptorClass, const b8 writable, Core::GpuDescriptorHeap& heap, Core::Buffer& buffer, Core::GpuDescriptorHandle& inOutHandle){
    const auto acquired = RegisterHeapBuffer(heap, buffer, descriptorClass, writable);
    if(!acquired)
        return false;
    Core::GpuDescriptorRetirementGuard retireUnpublished(heap, *acquired);
    if(inOutHandle.valid())
        heap.free(inOutHandle);
    inOutHandle = *acquired;
    retireUnpublished.release();
    return true;
}

void RetireHeapHandle(Core::GpuDescriptorHeap& heap, Core::GpuDescriptorHandle& handle){
    if(handle.valid())
        heap.free(handle);
    handle = Core::GpuDescriptorHandle::Invalid();
}

Core::ComputeState MakeComputePassState(const Core::ComputePipelineHandle& pipeline)noexcept{
    Core::ComputeState state;
    state.setPipeline(pipeline.get());
    // Set 0 is push-only; keep it so heap sets retain layout indices.
    return state;
}

// Binned-SAH BVH over indices[lo, hi); node 0 is root. Splits on the min SAH boundary, median on coincident centroids.
u32 BuildSceneBvhNode(const u32 lo, const u32 hi, const Span<const SceneBvhPrimitiveCalculation> primitiveBounds, const Span<const u32> instanceLeafCost, const Span<u32> indices, Vector<SceneBvhNodeCalculation, Core::Alloc::ScratchArena>& nodes){
    const u32 nodeIndex = static_cast<u32>(nodes.size());
    nodes.push_back(SceneBvhNodeCalculation{});

    if((hi - lo) == 1u){
        const u32 instance = indices[lo];
        SceneBvhNodeCalculation& node = nodes[nodeIndex];
        node.aabbMin = primitiveBounds[instance].aabbMin;
        node.aabbMax = primitiveBounds[instance].aabbMax;
        node.leftChild = BvhNodeIndex::LeafFlag | instance;
        node.rightChild = 1u;
        node.containsTransparentOccluder = primitiveBounds[instance].transparentOccluder;
        return nodeIndex;
    }

    // Primitive counts weight scene-level SAH so high-cost instances influence partitioning.

    // Centroid extent picks binable axes; a zero-extent axis separates nothing and is skipped.
    SIMDVectorSingle centroidMin = VectorReplicate(s_RayTracingFiniteInfinity);
    SIMDVectorSingle centroidMax = VectorReplicate(-s_RayTracingFiniteInfinity);
    for(u32 i = lo; i < hi; ++i){
        const SIMDVectorSingle centroid = LoadFloat(primitiveBounds[indices[i]].centroid);
        centroidMin = VectorMin(centroidMin, centroid);
        centroidMax = VectorMax(centroidMax, centroid);
    }

    // Per-axis bins: AABB, count, summed leaf cost. s_SceneBvhSahBinCount bins keeps the grid cache-resident.
    Array<SIMDVectorSingle, s_SceneBvhSahBinCount> binMin;
    Array<SIMDVectorSingle, s_SceneBvhSahBinCount> binMax;
    Array<f32, s_SceneBvhSahBinCount> binCost;
    Array<u32, s_SceneBvhSahBinCount> binCount;

    b8 anyAxisValid = false;
    u32 bestAxis = 0u;
    u32 bestBin = 0u;
    f32 bestCost = s_RayTracingFiniteInfinity;

    for(u32 axis = 0u; axis < s_SceneBvhAxisCount; ++axis){
        const f32 loExtent = VectorGetByIndex(centroidMin, axis);
        const f32 extent = VectorGetByIndex(centroidMax, axis) - loExtent;
        if(extent <= 0.0f)
            continue; // coincident centroids on this axis -> binning cannot split it
        anyAxisValid = true;
        const f32 invExtent = static_cast<f32>(s_SceneBvhSahBinCount) / extent;

        for(u32 b = 0u; b < s_SceneBvhSahBinCount; ++b){
            binMin[b] = VectorReplicate(s_RayTracingFiniteInfinity);
            binMax[b] = VectorReplicate(-s_RayTracingFiniteInfinity);
            binCost[b] = 0.0f;
            binCount[b] = 0u;
        }
        for(u32 i = lo; i < hi; ++i){
            const u32 instance = indices[i];
            const f32 c = primitiveBounds[instance].centroid.raw[axis];
            u32 b = static_cast<u32>((c - loExtent) * invExtent);
            if(b >= s_SceneBvhSahBinCount)
                b = s_SceneBvhSahBinCount - 1u;
            binMin[b] = VectorMin(binMin[b], LoadFloat(primitiveBounds[instance].aabbMin));
            binMax[b] = VectorMax(binMax[b], LoadFloat(primitiveBounds[instance].aabbMax));
            binCost[b] += static_cast<f32>(instanceLeafCost[instance]);
            ++binCount[b];
        }

        // Right-to-left suffix stats make every boundary's right side exact; then sweep left building the prefix SAH.
        Array<SIMDVectorSingle, s_SceneBvhSahBinCount> suffixMin;
        Array<SIMDVectorSingle, s_SceneBvhSahBinCount> suffixMax;
        Array<f32, s_SceneBvhSahBinCount> suffixCost;
        Array<u32, s_SceneBvhSahBinCount> suffixCount;
        {
            SIMDVectorSingle accMin = VectorReplicate(s_RayTracingFiniteInfinity);
            SIMDVectorSingle accMax = VectorReplicate(-s_RayTracingFiniteInfinity);
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

        // Boundary k splits at bin k: left = bins [0, k), right = bins [k, end).
        // The parent AABB is the bin union (suffix 0); same-slice comparison folds out the divisor, minimizing the raw cost.
        const SIMDVectorSingle parentMin = suffixMin[0u];
        const SIMDVectorSingle parentMax = suffixMax[0u];
        const f32 parentArea = VectorGetX(AabbTests::SurfaceArea(parentMin, parentMax));

        SIMDVectorSingle leftMin = VectorReplicate(s_RayTracingFiniteInfinity);
        SIMDVectorSingle leftMax = VectorReplicate(-s_RayTracingFiniteInfinity);
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

            const f32 leftArea = VectorGetX(AabbTests::SurfaceArea(leftMin, leftMax));
            const f32 rightArea = VectorGetX(AabbTests::SurfaceArea(suffixMin[k], suffixMax[k]));
            const f32 cost = parentArea * s_SceneBvhSahTraversalCost + leftArea * leftCost + rightArea * suffixCost[k];
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
        const f32 loExtent = VectorGetByIndex(centroidMin, bestAxis);
        const f32 extent = VectorGetByIndex(centroidMax, bestAxis) - loExtent;
        const f32 invExtent = static_cast<f32>(s_SceneBvhSahBinCount) / extent;
        mid = lo;
        for(u32 i = lo; i < hi; ++i){
            const u32 instance = indices[i];
            const f32 c = primitiveBounds[instance].centroid.raw[bestAxis];
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

    const u32 leftChild = BuildSceneBvhNode(lo, mid, primitiveBounds, instanceLeafCost, indices, nodes);
    const u32 rightChild = BuildSceneBvhNode(mid, hi, primitiveBounds, instanceLeafCost, indices, nodes);

    SceneBvhNodeCalculation& node = nodes[nodeIndex];
    StoreFloat(VectorMin(LoadFloat(nodes[leftChild].aabbMin), LoadFloat(nodes[rightChild].aabbMin)), node.aabbMin);
    StoreFloat(VectorMax(LoadFloat(nodes[leftChild].aabbMax), LoadFloat(nodes[rightChild].aabbMax)), node.aabbMax);
    node.leftChild = leftChild;
    node.rightChild = rightChild;
    node.containsTransparentOccluder = nodes[leftChild].containsTransparentOccluder || nodes[rightChild].containsTransparentOccluder;
    return nodeIndex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

