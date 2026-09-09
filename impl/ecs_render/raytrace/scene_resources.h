// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/raytracing.h>
#include <core/graphics/rhi/resource.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The ray-tracing domain owns the live scene and material tables. Camera effects retain this immutable snapshot
// after scene preflight, then declare the referenced buffers plus the separately frozen trace-geometry and material
// sampled-texture bundles. Availability means the resources can be scheduled; the scene preparation task must also
// succeed before a hardware query dispatch. No effect pipeline, output target, or temporal policy belongs here.
struct RayTracingSceneGraphResources{
    Core::RayTracingAccelStructHandle sceneTlas;
    Core::GpuDescriptorHandle tlasHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::BufferHandle materialContextSlotsBuffer;
    Core::BufferHandle instanceMaterialBuffer;
    Core::BufferHandle materialTypedBuffer;
    Core::BufferHandle instanceBuffer;
    u32 materialContextSlotsHeapSlot = 0u;
    bool hardwareAvailable = false;

    [[nodiscard]] bool valid()const noexcept{
        return
            hardwareAvailable && sceneTlas && tlasHeapHandle.valid() && materialContextSlotsBuffer
            && instanceMaterialBuffer && materialTypedBuffer && instanceBuffer
        ;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

