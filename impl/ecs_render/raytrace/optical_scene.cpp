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
{
    instances.reserve(capacity);
}

void RayTracingOpticalSceneGather::append(
    const Core::ECS::EntityID entity,
    const RendererComponent& renderer,
    const bool transparent,
    const Float3U& boundsMin,
    const Float3U& boundsMax,
    const bool boundsValid){
    RayTracingOpticalInstanceGpu instance;
    instance.entityId = entity.id;
    instance.mediumPriority = renderer.opticalMediumPriority;
    instance.boundaryMode = static_cast<u32>(renderer.opticalBoundaryMode);
    if(transparent){
        instance.flags |= NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT;
        ++header.transparentCount;
        const bool finiteBounds =
            boundsValid
            && IsFinite(boundsMin.x) && IsFinite(boundsMin.y) && IsFinite(boundsMin.z)
            && IsFinite(boundsMax.x) && IsFinite(boundsMax.y) && IsFinite(boundsMax.z)
            && boundsMin.x <= boundsMax.x && boundsMin.y <= boundsMax.y && boundsMin.z <= boundsMax.z
        ;
        if(finiteBounds){
            instance.flags |= NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID;
            if(!hasBounds){
                header.boundsMin = boundsMin;
                header.boundsMax = boundsMax;
                hasBounds = true;
            }
            else{
                header.boundsMin.x = Min(header.boundsMin.x, boundsMin.x);
                header.boundsMin.y = Min(header.boundsMin.y, boundsMin.y);
                header.boundsMin.z = Min(header.boundsMin.z, boundsMin.z);
                header.boundsMax.x = Max(header.boundsMax.x, boundsMax.x);
                header.boundsMax.y = Max(header.boundsMax.y, boundsMax.y);
                header.boundsMax.z = Max(header.boundsMax.z, boundsMax.z);
            }
        }
        else
            markIncomplete();
    }
    instances.push_back(instance);
}

u64 RayTracingOpticalSceneGather::contentHash()const noexcept{
    u64 hash = FNV64_OFFSET_BASIS;
    Fnv64AppendValue(hash, header);
    for(const auto& instance : instances)
        Fnv64AppendValue(hash, instance);
    return hash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool OpticalInstanceOrderMatches(
    const u8* const leftBytes,
    const usize leftCount,
    const RayTracingOpticalInstanceGpu* const right,
    const usize rightCount)noexcept{
    if(leftCount != rightCount || ((!leftBytes || !right) && leftCount != 0u))
        return false;
    for(usize index = 0u; index < leftCount; ++index){
        u32 entityId = 0u;
        NWB_MEMCPY(&entityId, sizeof(entityId), leftBytes + index * NWB_RT_OPTICAL_INSTANCE_BYTES, sizeof(entityId));
        if(entityId != right[index].entityId)
            return false;
    }
    return true;
}

bool ComputeOpticalWorldBounds(
    const Float34U& objectToWorld,
    const Float3U& localMin,
    const Float3U& localMax,
    Float3U& outMin,
    Float3U& outMax)noexcept{
    // A float affine coordinate uses three products and three additions. Gamma(8) additionally covers the final
    // float bound conversion and margin arithmetic. The tiny absolute term covers flushed subnormal intermediates.
    constexpr f64 s_UnitRoundoff = 0x1p-24;
    constexpr f64 s_TransformError = (8.0 * s_UnitRoundoff) / (1.0 - 8.0 * s_UnitRoundoff);
    constexpr f64 s_UnderflowMargin = 8.0 * 0x1p-126;
    for(usize axis = 0u; axis < 3u; ++axis){
        if(!IsFinite(localMin.raw[axis]) || !IsFinite(localMax.raw[axis]) || localMin.raw[axis] > localMax.raw[axis])
            return false;
    }
    Float3U minimum{};
    Float3U maximum{};
    for(usize row = 0u; row < 3u; ++row){
        if(!IsFinite(objectToWorld.m[row][3u]))
            return false;
        f64 low = objectToWorld.m[row][3u];
        f64 high = low;
        f64 magnitude = Abs(low);
        for(usize column = 0u; column < 3u; ++column){
            if(!IsFinite(objectToWorld.m[row][column]))
                return false;
            const f64 coefficient = objectToWorld.m[row][column];
            const f64 first = coefficient * static_cast<f64>(localMin.raw[column]);
            const f64 second = coefficient * static_cast<f64>(localMax.raw[column]);
            low += Min(first, second);
            high += Max(first, second);
            magnitude += Max(Abs(first), Abs(second));
        }
        const f64 margin = s_TransformError * magnitude + s_UnderflowMargin;
        minimum.raw[row] = static_cast<f32>(low - margin);
        maximum.raw[row] = static_cast<f32>(high + margin);
        if(!IsFinite(minimum.raw[row]) || !IsFinite(maximum.raw[row]))
            return false;
    }
    outMin = minimum;
    outMax = maximum;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

