// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/assets/graphics/raytrace/optical_scene_constants.h>

#include <core/alloc/scratch.h>
#include <core/ecs/entity_id.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/resource.h>


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

struct RayTracingOpticalRuntimeBounds{
    Core::BufferHandle buffer;
    Core::GpuDescriptorHandle descriptor = Core::GpuDescriptorHandle::invalid();
    Float34U objectToWorld = {};
    u32 instanceIndex = 0u;
};

// Built in the existing RT gather, after duplicate suppression and with the exact emitted instance ordering.
// Invalid or omitted geometry clears the air-shortcut proof, including when no known transparent instance remains.
struct RayTracingOpticalSceneGather{
    RayTracingOpticalSceneHeaderGpu header;
    Vector<RayTracingOpticalInstanceGpu, Core::Alloc::ScratchArena> instances;
    Vector<RayTracingOpticalRuntimeBounds, Core::Alloc::ScratchArena> runtimeBounds;
    bool hasBounds = false;
    bool boundsCompleteExceptRuntime = true;
    // Policy-only specialization; incomplete geometry and bounds still reject transport in the shader.
    bool unspecifiedBoundariesOnly = true;

    explicit RayTracingOpticalSceneGather(Core::Alloc::ScratchArena& arena, usize capacity);
    void append(
        Core::ECS::EntityID entity,
        const RendererComponent& renderer,
        bool transparent,
        const Float3U& boundsMin,
        const Float3U& boundsMax,
        bool boundsValid
    );
    void appendRuntime(
        Core::ECS::EntityID entity,
        const RendererComponent& renderer,
        const Core::BufferHandle& boundsBuffer,
        Core::GpuDescriptorHandle boundsDescriptor,
        const Float34U& objectToWorld
    );
    void markIncomplete()noexcept{
        header.flags &= ~NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID;
        boundsCompleteExceptRuntime = false;
    }
    [[nodiscard]] u64 contentHash()const noexcept;
};

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

