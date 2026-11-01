// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "optical_scene.h"

#include <impl/ecs_render/components.adhd.h>

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(OpticalBoundaryMode::Unspecified == NWB_RT_OPTICAL_BOUNDARY_UNSPECIFIED);
static_assert(OpticalBoundaryMode::ClosedNested == NWB_RT_OPTICAL_BOUNDARY_CLOSED_NESTED);
static_assert(OpticalBoundaryMode::ClosedPriority == NWB_RT_OPTICAL_BOUNDARY_CLOSED_PRIORITY);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RayTracingOpticalSceneGather::append(const Core::ECS::EntityID entity, const RendererComponent& renderer, const b8 transparent, const SIMDVectorSingle boundsMin, const SIMDVectorSingle boundsMax, const b8 boundsValid){
    RayTracingOpticalInstanceGpu instance;
    instance.entityId = entity.id;
    instance.mediumPriority = renderer.opticalMediumPriority;
    instance.boundaryMode = static_cast<u32>(renderer.opticalBoundaryMode);
    if(transparent){
        if(renderer.opticalBoundaryMode != OpticalBoundaryMode::Unspecified)
            unspecifiedBoundariesOnly = false;
        instance.flags |= NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT;
        ++header.transparentCount;
        const b8 finiteBounds = boundsValid && Vector3IsFinite(boundsMin) && Vector3IsFinite(boundsMax) && Vector3LessOrEqual(boundsMin, boundsMax);
        if(finiteBounds){
            instance.flags |= NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID;
            if(!hasBounds){
                StoreFloat(boundsMin, header.boundsMin);
                StoreFloat(boundsMax, header.boundsMax);
                hasBounds = true;
            }
            else{
                StoreFloat(VectorMin(LoadFloat(header.boundsMin), boundsMin), header.boundsMin);
                StoreFloat(VectorMax(LoadFloat(header.boundsMax), boundsMax), header.boundsMax);
            }
        }
        else
            markIncomplete();
    }
    instances.push_back(instance);
}

void RayTracingOpticalSceneGather::appendRuntime(const Core::ECS::EntityID entity, const RendererComponent& renderer, const Core::BufferHandle& boundsBuffer, const Core::GpuDescriptorHandle boundsDescriptor, const Float34& objectToWorld){
    const b8 staticComplete = boundsCompleteExceptRuntime;
    const u32 instanceIndex = static_cast<u32>(instances.size());
    append(entity, renderer, true, VectorZero(), VectorZero(), false);
    // The CPU upload stays deliberately incomplete. Only the GPU finalizer can validate this contributor.
    if(!boundsBuffer || !boundsDescriptor.valid() || boundsDescriptor.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer)
        return;
    boundsCompleteExceptRuntime = staticComplete;
    if(runtimeBounds.empty())
        runtimeBounds.reserve(instances.capacity());
    runtimeBounds.push_back({ boundsBuffer, boundsDescriptor, instanceIndex, objectToWorld });
}

void RayTracingOpticalSceneGather::markIncomplete()noexcept{
    header.flags &= ~NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID;
    boundsCompleteExceptRuntime = false;
}

u64 RayTracingOpticalSceneGather::contentHash()const noexcept{
    u64 hash = s_Fnv64OffsetBasis;
    Fnv64AppendValue(hash, header);
    for(const auto& instance : instances)
        Fnv64AppendValue(hash, instance);
    if(!runtimeBounds.empty())
        Fnv64AppendValue(hash, boundsCompleteExceptRuntime);
    for(const auto& bounds : runtimeBounds){
        Fnv64AppendValue(hash, bounds.objectToWorld);
        Fnv64AppendValue(hash, bounds.instanceIndex);
    }
    return hash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<OpticalWorldBounds> NWB_SIMD_CALL ComputeOpticalWorldBounds(const SIMDMatrix& objectToWorld, const SIMDVectorSingle localMin, const SIMDVectorSingle localMax)noexcept{
    Float4 row0;
    Float4 row1;
    Float4 row2;
    Float4 localMinData;
    Float4 localMaxData;
    StoreFloat(objectToWorld.v[0u], row0);
    StoreFloat(objectToWorld.v[1u], row1);
    StoreFloat(objectToWorld.v[2u], row2);
    StoreFloat(VectorAndInt(localMin, s_SIMDMask3), localMinData);
    StoreFloat(VectorAndInt(localMax, s_SIMDMask3), localMaxData);
    OpticalWorldBoundsData outBounds;
    if(!TryComputeOpticalWorldBounds(row0, row1, row2, localMinData, localMaxData, outBounds))
        return MakeUnexpected(Failure{});
    return OpticalWorldBounds{ LoadFloat(outBounds.minimum), LoadFloat(outBounds.maximum) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

