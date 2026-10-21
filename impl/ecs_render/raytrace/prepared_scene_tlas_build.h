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
// RayTracingInstanceDesc stores raw BLAS pointers, so the plan retains every corresponding BLAS handle plus the selected TLAS and backing generation until Shadow Preparation accepts or discards it.
class PreparedSceneTlasBuild final : NoCopy{
public:
    explicit PreparedSceneTlasBuild(Core::Alloc::GlobalArena& arena)noexcept
        : m_instances(arena)
        , m_blases(arena)
    {}


public:
    [[nodiscard]] bool capture(
        const RendererRayTracingState& state,
        bool staticScene,
        u64 staticSceneHash,
        const Vector<Core::RayTracingInstanceDesc, Core::Alloc::ScratchArena>& instances,
        const Vector<Core::RayTracingAccelStructHandle, Core::Alloc::ScratchArena>& instanceBlases
    );
    [[nodiscard]] bool record(RendererRayTracingState& state, Core::CommandList& commandList);
    // Publishes the accepted static-scene hash and backing handoff, then clears the plan.
    void confirm(RendererRayTracingState& state)noexcept;
    void clear()noexcept;
    [[nodiscard]] bool ready()const noexcept{ return m_ready; }


private:
    Vector<Core::RayTracingInstanceDesc, Core::Alloc::GlobalArena> m_instances;
    Vector<Core::RayTracingAccelStructHandle, Core::Alloc::GlobalArena> m_blases;
    Core::RayTracingAccelStructHandle m_tlas;
    Core::BufferHandle m_backingBuffer;
    usize m_maxInstances = 0u;
    u64 m_staticSceneHash = 0u;
    Core::GpuDescriptorHandle m_heapHandle;
    bool m_static = false;
    bool m_ready = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

