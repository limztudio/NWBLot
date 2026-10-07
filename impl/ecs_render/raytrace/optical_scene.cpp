// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "optical_scene.h"

#include <impl/ecs_render/components.h>
#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(OpticalBoundaryMode::Unspecified == NWB_RT_OPTICAL_BOUNDARY_UNSPECIFIED);
static_assert(OpticalBoundaryMode::ClosedNested == NWB_RT_OPTICAL_BOUNDARY_CLOSED_NESTED);
static_assert(OpticalBoundaryMode::ClosedPriority == NWB_RT_OPTICAL_BOUNDARY_CLOSED_PRIORITY);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingOpticalSceneGather::RayTracingOpticalSceneGather(Core::Alloc::ScratchArena& arena, const usize capacity)
    : instances(arena)
    , runtimeBounds(arena)
{
    instances.reserve(capacity);
}

void RayTracingOpticalSceneGather::append(
    const Core::ECS::EntityID entity,
    const RendererComponent& renderer,
    const bool transparent,
    const Float3U& boundsMin,
    const Float3U& boundsMax,
    const bool boundsValid
){
    RayTracingOpticalInstanceGpu instance;
    instance.entityId = entity.id;
    instance.mediumPriority = renderer.opticalMediumPriority;
    instance.boundaryMode = static_cast<u32>(renderer.opticalBoundaryMode);
    if(transparent){
        if(renderer.opticalBoundaryMode != OpticalBoundaryMode::Unspecified)
            unspecifiedBoundariesOnly = false;
        instance.flags |= NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT;
        ++header.transparentCount;
        const SIMDVector boundsMinVector = LoadFloat(boundsMin);
        const SIMDVector boundsMaxVector = LoadFloat(boundsMax);
        const bool finiteBounds =
            boundsValid
            && Vector3IsFinite(boundsMinVector)
            && Vector3IsFinite(boundsMaxVector)
            && Vector3LessOrEqual(boundsMinVector, boundsMaxVector)
        ;
        if(finiteBounds){
            instance.flags |= NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID;
            if(!hasBounds){
                header.boundsMin = boundsMin;
                header.boundsMax = boundsMax;
                hasBounds = true;
            }
            else{
                StoreFloat(VectorMin(LoadFloat(header.boundsMin), boundsMinVector), header.boundsMin);
                StoreFloat(VectorMax(LoadFloat(header.boundsMax), boundsMaxVector), header.boundsMax);
            }
        }
        else
            markIncomplete();
    }
    instances.push_back(instance);
}

void RayTracingOpticalSceneGather::appendRuntime(
    const Core::ECS::EntityID entity,
    const RendererComponent& renderer,
    const Core::BufferHandle& boundsBuffer,
    const Core::GpuDescriptorHandle boundsDescriptor,
    const Float34U& objectToWorld
){
    const bool staticComplete = boundsCompleteExceptRuntime;
    const u32 instanceIndex = static_cast<u32>(instances.size());
    append(entity, renderer, true, {}, {}, false);
    // The CPU upload stays deliberately incomplete. Only the GPU finalizer can validate this contributor.
    if(!boundsBuffer || !boundsDescriptor.valid() || boundsDescriptor.descriptorClass() != Core::GpuDescriptorClass::StorageBuffer)
        return;
    boundsCompleteExceptRuntime = staticComplete;
    if(runtimeBounds.empty())
        runtimeBounds.reserve(instances.capacity());
    runtimeBounds.push_back({ boundsBuffer, boundsDescriptor, objectToWorld, instanceIndex });
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


bool ComputeOpticalWorldBounds(
    const Float34U& objectToWorld,
    const Float3U& localMin,
    const Float3U& localMax,
    Float3U& outMin,
    Float3U& outMax
)noexcept{
    // SIMD lanes own the affine corner math: all eight AABB corners are transformed as one
    // lane batch, and the enclosure margin is reduced from lane magnitudes on vector lanes.
    // A float affine coordinate uses three products and three additions. Gamma(8) additionally covers the final
    // float bound conversion and margin arithmetic. The tiny absolute term covers flushed subnormal intermediates.
    constexpr f64 s_UnitRoundoff = 0x1p-24;
    constexpr f64 s_TransformError = (8.0 * s_UnitRoundoff) / (1.0 - 8.0 * s_UnitRoundoff);
    constexpr f64 s_UnderflowMargin = 8.0 * 0x1p-126;
    const SIMDVector localMinVec = LoadFloat(localMin);
    const SIMDVector localMaxVec = LoadFloat(localMax);
    if(!VectorIsFinite(localMinVec, VectorComponentMask::s_XYZ) || !VectorIsFinite(localMaxVec, VectorComponentMask::s_XYZ))
        return false;
    if(!Vector3LessOrEqual(localMinVec, localMaxVec))
        return false;
    const SIMDVector row0 = LoadFloat(objectToWorld.rows[0u]);
    const SIMDVector row1 = LoadFloat(objectToWorld.rows[1u]);
    const SIMDVector row2 = LoadFloat(objectToWorld.rows[2u]);
    if(!VectorIsFinite(row0, VectorComponentMask::s_XYZW) || !VectorIsFinite(row1, VectorComponentMask::s_XYZW) || !VectorIsFinite(row2, VectorComponentMask::s_XYZW))
        return false;
    SIMDVector laneMin = VectorReplicate(Limit<f32>::s_Infinity);
    SIMDVector laneMax = VectorReplicate(-Limit<f32>::s_Infinity);
    SIMDVector laneMagnitude = VectorZero();
    for(u32 corner = 0u; corner < 8u; ++corner){
        const SIMDVector selectX = ((corner & 1u) != 0u) ? localMaxVec : localMinVec;
        const SIMDVector selectY = ((corner & 2u) != 0u) ? localMaxVec : localMinVec;
        const SIMDVector selectZ = ((corner & 4u) != 0u) ? localMaxVec : localMinVec;
        SIMDVector point = VectorSelect(selectX, selectY, s_SIMDMaskY);
        point = VectorSelect(point, selectZ, s_SIMDMaskZ);
        const SIMDVector x = VectorSplatX(point);
        const SIMDVector y = VectorSplatY(point);
        const SIMDVector z = VectorSplatZ(point);
        SIMDVector transformed = VectorMultiply(VectorSplatX(row0), x);
        transformed = VectorMultiplyAdd(VectorSplatY(row0), y, transformed);
        transformed = VectorMultiplyAdd(VectorSplatZ(row0), z, transformed);
        SIMDVector transformed1 = VectorMultiply(VectorSplatX(row1), x);
        transformed1 = VectorMultiplyAdd(VectorSplatY(row1), y, transformed1);
        transformed1 = VectorMultiplyAdd(VectorSplatZ(row1), z, transformed1);
        SIMDVector transformed2 = VectorMultiply(VectorSplatX(row2), x);
        transformed2 = VectorMultiplyAdd(VectorSplatY(row2), y, transformed2);
        transformed2 = VectorMultiplyAdd(VectorSplatZ(row2), z, transformed2);
        SIMDVector cornerVec = VectorSelect(transformed, transformed1, s_SIMDMaskY);
        cornerVec = VectorSelect(cornerVec, transformed2, s_SIMDMaskZ);
        // Float34U rows pack translation in W lanes (_14/_24/_34); W lane itself stays zero.
        cornerVec = VectorAdd(cornerVec, VectorSet(VectorGetW(row0), VectorGetW(row1), VectorGetW(row2), 0.0f));
        if(!VectorIsFinite(cornerVec, VectorComponentMask::s_XYZ))
            return false;
        laneMin = VectorMin(laneMin, cornerVec);
        laneMax = VectorMax(laneMax, cornerVec);
        laneMagnitude = VectorMax(laneMagnitude, VectorAbs(cornerVec));
    }
    const SIMDVector magnitudeSum = VectorAdd(VectorAdd(VectorSplatX(laneMagnitude), VectorSplatY(laneMagnitude)), VectorSplatZ(laneMagnitude));
    const SIMDVector marginVec = VectorAdd(VectorMultiply(VectorReplicate(static_cast<f32>(s_TransformError)), magnitudeSum), VectorReplicate(static_cast<f32>(s_UnderflowMargin)));
    const SIMDVector minimum = VectorSubtract(laneMin, marginVec);
    const SIMDVector maximum = VectorAdd(laneMax, marginVec);
    if(!VectorIsFinite(minimum, VectorComponentMask::s_XYZ) || !VectorIsFinite(maximum, VectorComponentMask::s_XYZ))
        return false;
    StoreFloat(minimum, outMin);
    StoreFloat(maximum, outMax);
    return true;
}



NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

