// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/alloc/scratch.h>
#include <core/graphics/rhi/device.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingState;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hardware TLAS work records from this frozen preflight plan in Shadow Preparation; its static cache becomes valid only after that packet accepts.
// Instance descs hold raw BLAS pointers; the plan retains every BLAS plus TLAS/backing until Shadow Preparation settles.
class PreparedSceneTlasBuild final : NoCopy{
public:
    explicit PreparedSceneTlasBuild(Core::Alloc::GlobalArena& arena)noexcept
        : m_instances(arena)
        , m_blases(arena)
    {}


public:
    [[nodiscard]] b8 capture(
        const RendererRayTracingState& state,
        b8 staticScene,
        u64 staticSceneHash,
        Span<const Core::RayTracingInstanceDesc> instances,
        Span<const Core::RayTracingAccelStructHandle> instanceBlases
    );
    [[nodiscard]] b8 record(RendererRayTracingState& state, Core::CommandList& commandList);
    // Publishes the accepted static-scene hash and backing handoff, then clears the plan.
    void confirm(RendererRayTracingState& state)noexcept;
    void clear()noexcept;
    [[nodiscard]] b8 ready()const noexcept{ return m_ready; }


private:
    Vector<Core::RayTracingInstanceDesc, Core::Alloc::GlobalArena> m_instances;
    Vector<Core::RayTracingAccelStructHandle, Core::Alloc::GlobalArena> m_blases;
    Core::RayTracingAccelStructHandle m_tlas;
    Core::BufferHandle m_backingBuffer;
    usize m_maxInstances = 0u;
    u64 m_staticSceneHash = 0u;
    Core::GpuDescriptorHandle m_heapHandle;
    b8 m_static = false;
    b8 m_ready = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

