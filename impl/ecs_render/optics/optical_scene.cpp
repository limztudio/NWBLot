// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "optical_scene.h"

#include <impl/ecs_render/optics/optical_bounds_evaluator.adhd.h>
#include <impl/ecs_render/optics/optical_scene_identity_builder.adhd.h>

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
    const RayTracingOpticalInstanceGpu instance = BuildRayTracingOpticalInstance(
        entity,
        renderer,
        transparent,
        boundsMin,
        boundsMax,
        boundsValid,
        header,
        hasBounds,
        boundsCompleteExceptRuntime,
        unspecifiedBoundariesOnly
    );
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
    runtimeBounds.push_back({ boundsBuffer, boundsDescriptor, objectToWorld, instanceIndex });
}

void RayTracingOpticalSceneGather::markIncomplete()noexcept{
    MarkRayTracingOpticalSceneIncomplete(header, boundsCompleteExceptRuntime);
}

u64 RayTracingOpticalSceneGather::contentHash()const noexcept{
    u64 hash = OpticalSceneIdentityBuilder{}.baseHash(
        header,
        Span<const RayTracingOpticalInstanceGpu>(instances.data(), instances.size()),
        !runtimeBounds.empty(),
        boundsCompleteExceptRuntime
    );
    for(const auto& bounds : runtimeBounds)
        hash = OpticalSceneIdentityBuilder{}.updateRuntimeBounds(hash, LoadFloat(bounds.objectToWorld), bounds.instanceIndex);
    return hash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<OpticalWorldBounds> NWB_SIMD_CALL ComputeOpticalWorldBounds(const SIMDMatrix& objectToWorld, const SIMDVectorSingle localMin, const SIMDVectorSingle localMax)noexcept{
    OpticalWorldBounds bounds{};
    if(!OpticalBoundsEvaluator{}.computeWorldBounds(objectToWorld, localMin, localMax, bounds.minimum, bounds.maximum))
        return MakeUnexpected(Failure{});
    return bounds;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

