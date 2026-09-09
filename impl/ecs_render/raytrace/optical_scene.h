// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/assets/graphics/raytrace/optical_scene_constants.h>

#include <core/alloc/scratch.h>
#include <core/ecs/entity_id.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RendererComponent;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RayTracingOpticalSceneHeaderGpu{
    Float3U boundsMin = {};
    u32 transparentCount = 0u;
    Float3U boundsMax = {};
    u32 flags = NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID;
};
static_assert(sizeof(RayTracingOpticalSceneHeaderGpu) == NWB_RT_OPTICAL_SCENE_HEADER_BYTES);
static_assert(offsetof(RayTracingOpticalSceneHeaderGpu, transparentCount) == NWB_RT_OPTICAL_SCENE_TRANSPARENT_COUNT_OFFSET);
static_assert(offsetof(RayTracingOpticalSceneHeaderGpu, flags) == NWB_RT_OPTICAL_SCENE_FLAGS_OFFSET);

struct RayTracingOpticalInstanceGpu{
    u32 entityId = 0u;
    i32 mediumPriority = 0;
    u32 boundaryMode = NWB_RT_OPTICAL_BOUNDARY_UNSPECIFIED;
    u32 flags = 0u;
};
static_assert(sizeof(RayTracingOpticalInstanceGpu) == NWB_RT_OPTICAL_INSTANCE_BYTES);
static_assert(offsetof(RayTracingOpticalInstanceGpu, mediumPriority) == NWB_RT_OPTICAL_INSTANCE_PRIORITY_OFFSET);
static_assert(offsetof(RayTracingOpticalInstanceGpu, boundaryMode) == NWB_RT_OPTICAL_INSTANCE_BOUNDARY_OFFSET);
static_assert(offsetof(RayTracingOpticalInstanceGpu, flags) == NWB_RT_OPTICAL_INSTANCE_FLAGS_OFFSET);

// Built in the existing RT gather, after duplicate suppression and with the exact emitted instance ordering.
// Invalid or omitted geometry clears the air-shortcut proof, including when no known transparent instance remains.
struct RayTracingOpticalSceneGather{
    RayTracingOpticalSceneHeaderGpu header;
    Vector<RayTracingOpticalInstanceGpu, Core::Alloc::ScratchArena> instances;
    bool hasBounds = false;

    explicit RayTracingOpticalSceneGather(Core::Alloc::ScratchArena& arena, usize capacity);
    void append(
        Core::ECS::EntityID entity,
        const RendererComponent& renderer,
        bool transparent,
        const Float3U& boundsMin,
        const Float3U& boundsMax,
        bool boundsValid
    );
    void markIncomplete()noexcept{ header.flags &= ~NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID; }
    [[nodiscard]] u64 contentHash()const noexcept;
};

[[nodiscard]] bool OpticalInstanceOrderMatches(
    const u8* leftBytes,
    usize leftCount,
    const RayTracingOpticalInstanceGpu* right,
    usize rightCount
)noexcept;

// Enclose the frozen float affine transform, including its finite-precision evaluation on CPU/GPU.
[[nodiscard]] bool ComputeOpticalWorldBounds(
    const Float34U& objectToWorld,
    const Float3U& localMin,
    const Float3U& localMax,
    Float3U& outMin,
    Float3U& outMax
)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

