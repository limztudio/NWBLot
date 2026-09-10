// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "optical_scene.h"

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


// Constructed during preflight and never mutated after publication. Graph declarations copy these bytes into
// their own immutable upload storage; retained snapshots also pin the exact CPU and GPU generations.
struct RayTracingOpticalSceneUpload{
    Vector<u8, Core::Alloc::GlobalArena> bytes;
    u32 instanceCount = 0u;

    RayTracingOpticalSceneUpload(Core::Alloc::GlobalArena& arena, const RayTracingOpticalSceneGather& gather);
};
using RayTracingOpticalSceneUploadControl = RefCounter<RayTracingOpticalSceneUpload>;
using RayTracingOpticalSceneUploadHandle = RefCountPtr<RayTracingOpticalSceneUploadControl, ArenaRefDeleter<RayTracingOpticalSceneUploadControl, Core::Alloc::GlobalArena>>;

struct RayTracingOpticalSceneSnapshot{
    Core::BufferHandle buffer;
    Core::GpuDescriptorHandle descriptor = Core::GpuDescriptorHandle::invalid();
    RayTracingOpticalSceneUploadHandle upload;
    u32 transparentCount = 0u;
    bool boundsComplete = false;

    [[nodiscard]] bool valid()const noexcept{ return buffer && descriptor.valid() && upload; }
};

class RayTracingOpticalSceneResources final : NoCopy{
public:
    RayTracingOpticalSceneResources(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, Name identity);

public:
    void invalidate();
    void resetPrepared()noexcept{ m_prepared = false; }
    [[nodiscard]] bool prepare(const RayTracingOpticalSceneGather& gather);
    [[nodiscard]] bool matchesInstanceOrder(const RayTracingOpticalSceneGather& gather)const noexcept;
    [[nodiscard]] RayTracingOpticalSceneSnapshot snapshot()const;

private:
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    Name m_identity;
    RayTracingOpticalSceneSnapshot m_resources;
    usize m_capacity = 0u;
    bool m_prepared = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

