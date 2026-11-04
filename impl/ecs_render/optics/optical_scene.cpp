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
    runtimeBounds.push_back({ boundsBuffer, boundsDescriptor, instanceIndex, objectToWorld });
}

void RayTracingOpticalSceneGather::markIncomplete()noexcept{
    MarkRayTracingOpticalSceneIncomplete(header, boundsCompleteExceptRuntime);
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
    const SIMDVectorSingle localMinVec = VectorAndInt(localMin, s_SIMDMask3);
    const SIMDVectorSingle localMaxVec = VectorAndInt(localMax, s_SIMDMask3);
    if(!VectorIsFinite(localMinVec, VectorComponentMask::s_XYZ) || !VectorIsFinite(localMaxVec, VectorComponentMask::s_XYZ) || !Vector3LessOrEqual(localMinVec, localMaxVec))
        return MakeUnexpected(Failure{});

    const SIMDVectorSingle row0Vec = objectToWorld.v[0u];
    const SIMDVectorSingle row1Vec = objectToWorld.v[1u];
    const SIMDVectorSingle row2Vec = objectToWorld.v[2u];
    if(!VectorIsFinite(row0Vec, VectorComponentMask::s_XYZW) || !VectorIsFinite(row1Vec, VectorComponentMask::s_XYZW) || !VectorIsFinite(row2Vec, VectorComponentMask::s_XYZW))
        return MakeUnexpected(Failure{});

    const SIMDMatrix columns = MatrixTranspose(SIMDMatrix{ .v = { row0Vec, row1Vec, row2Vec, VectorZero() } });
    SIMDVectorSingle laneMin = VectorReplicate(Limit<f32>::s_Infinity);
    SIMDVectorSingle laneMax = VectorReplicate(-Limit<f32>::s_Infinity);
    SIMDVectorSingle laneMagnitude = VectorZero();
    for(u32 corner = 0u; corner < s_OpticalBoxCornerCount; ++corner){
        const SIMDVectorSingle selectX = ((corner & s_CornerMaxXBit) != 0u) ? localMaxVec : localMinVec;
        const SIMDVectorSingle selectY = ((corner & s_CornerMaxYBit) != 0u) ? localMaxVec : localMinVec;
        const SIMDVectorSingle selectZ = ((corner & s_CornerMaxZBit) != 0u) ? localMaxVec : localMinVec;
        SIMDVectorSingle point = VectorSelect(selectX, selectY, s_SIMDMaskY);
        point = VectorSelect(point, selectZ, s_SIMDMaskZ);
        const SIMDVectorSingle x = VectorSplatX(point);
        const SIMDVectorSingle y = VectorSplatY(point);
        const SIMDVectorSingle z = VectorSplatZ(point);
        SIMDVectorSingle cornerVec = VectorMultiply(columns.v[0], x);
        cornerVec = VectorMultiplyAdd(columns.v[1], y, cornerVec);
        cornerVec = VectorMultiplyAdd(columns.v[2], z, cornerVec);
        cornerVec = VectorAdd(cornerVec, columns.v[3]);
        if(!VectorIsFinite(cornerVec, VectorComponentMask::s_XYZ))
            return MakeUnexpected(Failure{});
        laneMin = VectorMin(laneMin, cornerVec);
        laneMax = VectorMax(laneMax, cornerVec);
        laneMagnitude = VectorMax(laneMagnitude, VectorAbs(cornerVec));
    }

    const SIMDVectorSingle magnitudeSum = VectorAdd(VectorAdd(VectorSplatX(laneMagnitude), VectorSplatY(laneMagnitude)), VectorSplatZ(laneMagnitude));
    const SIMDVectorSingle marginVec = VectorAdd(VectorMultiply(VectorReplicate(static_cast<f32>(s_OpticalTransformError)), magnitudeSum), VectorReplicate(static_cast<f32>(s_OpticalUnderflowMargin)));
    const SIMDVectorSingle minimum = VectorSubtract(laneMin, marginVec);
    const SIMDVectorSingle maximum = VectorAdd(laneMax, marginVec);
    if(!VectorIsFinite(minimum, VectorComponentMask::s_XYZ) || !VectorIsFinite(maximum, VectorComponentMask::s_XYZ))
        return MakeUnexpected(Failure{});
    return OpticalWorldBounds{ VectorAndInt(minimum, s_SIMDMask3), VectorAndInt(maximum, s_SIMDMask3) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

