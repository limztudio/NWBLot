// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "optical_scene_resources.h"

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/raytracing.h>
#include <core/graphics/rhi/resource.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Semantic identities selected by the current scene preflight, including direct transform and material edits.
// Runtime deformation is unstamped. Static mesh, texture and shader resources remain immutable until invalidation.
struct RayTracingSceneContentStamp{
    u64 geometry = 0u;
    u64 material = 0u;
    bool trusted = false;
};

// The ray-tracing domain owns the live scene and material tables. Camera effects retain this immutable snapshot
// after scene preflight, then declare the referenced buffers plus the separately frozen trace-geometry and material
// sampled-texture bundles. Availability means the resources can be scheduled; the scene preparation task must also
// succeed before a hardware query dispatch. No effect pipeline, output target, or temporal policy belongs here.
struct RayTracingSceneGraphResources{
    RayTracingSceneContentStamp contentStamp;
    Core::RayTracingAccelStructHandle sceneTlas;
    Core::GpuDescriptorHandle tlasHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::BufferHandle materialContextSlotsBuffer;
    Core::BufferHandle instanceMaterialBuffer;
    Core::BufferHandle materialTypedBuffer;
    Core::BufferHandle instanceBuffer;
    RayTracingOpticalSceneSnapshot opticalScene;
    u32 materialContextSlotsHeapSlot = 0u;
    bool hardwareAvailable = false;

    [[nodiscard]] bool valid()const noexcept{
        return
            hardwareAvailable && sceneTlas && tlasHeapHandle.valid() && materialContextSlotsBuffer
            && instanceMaterialBuffer && materialTypedBuffer && instanceBuffer && opticalScene.valid()
        ;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

