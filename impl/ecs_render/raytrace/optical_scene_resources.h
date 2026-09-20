// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "optical_scene_upload.h"
#include "optical_bounds_finalize.h"

#include <core/alloc/global.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/resource.h>
#include <global/arena_object.h>
#include <global/refcount_ptr.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RayTracingOpticalSceneSnapshot{
    Core::BufferHandle buffer;
    // Present only for runtime bounds: this remains the immutable CPU metadata upload destination.
    Core::BufferHandle uploadBuffer;
    RayTracingOpticalBoundsFinalizeHandle finalize;
    RayTracingOpticalSceneUploadHandle upload;
    RayTracingOpticalUploadControlHandle uploadState;
    Core::GpuDescriptorHandle descriptor = Core::GpuDescriptorHandle::invalid();
    u32 transparentCount = 0u;
    bool boundsComplete = false;
    bool unspecifiedBoundariesOnly = false;

    [[nodiscard]] bool valid()const noexcept{ return buffer && descriptor.valid() && upload && uploadState; }
};

class RayTracingOpticalSceneResources final : NoCopy{
public:
    RayTracingOpticalSceneResources(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, Name identity);

public:
    void invalidate();
    void resetPrepared()noexcept{ m_prepared = false; }
    [[nodiscard]] bool prepare(const RayTracingOpticalSceneGather& gather);
    [[nodiscard]] bool prepareRuntimeBounds(const RayTracingOpticalSceneGather& gather, RendererShaderSystem& shaderSystem);
    [[nodiscard]] RayTracingOpticalSceneSnapshot snapshot()const;

private:
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    Name m_identity;
    RayTracingOpticalSceneSnapshot m_resources;
    RayTracingOpticalBoundsFinalizeResources m_runtimeBounds;
    RayTracingOpticalBoundsFinalizeHandle m_runtimeBoundsSnapshot;
    usize m_capacity = 0u;
    bool m_prepared = false;
    bool m_runtimeBoundsRequired = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

