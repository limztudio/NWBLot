// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/mesh/renderer_mesh_types.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>

#include <core/task/gpu/types.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Declaration-only ownership of one complete generated group; never carry this state across graph/frame resets.
class AvboitGeneratedGeometryReuse final : NoCopy{
public:
    explicit AvboitGeneratedGeometryReuse(Core::Alloc::ScratchArena& arena)noexcept
        : m_arena(arena)
        , m_draws(arena)
        , m_instances(arena)
    {}


public:
    void reset()noexcept;
    [[nodiscard]] b8 capture(const MaterialPassDrawItemPartitionsView& draws, Span<const InstanceGpuData> instances, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings, const MeshViewGpuData& view, MaterialPipelinePass::Enum pass);
    [[nodiscard]] b8 matches(const MaterialPassDrawItemPartitionsView& draws, Span<const InstanceGpuData> instances, const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings, const MeshViewGpuData& view, MaterialPipelinePass::Enum pass);
    // Publish the generation task or a dependent completion task proving all output writes; generation must disable pass-scissor culling.
    [[nodiscard]] b8 publishProducer(Core::GpuTaskId task)noexcept;
    [[nodiscard]] Core::GpuTaskId producerTask()const noexcept{ return m_producer; }


private:
    Core::Alloc::ScratchArena& m_arena;
    MaterialPassDrawItemVector m_draws;
    InstanceGpuDataVector m_instances;
    ECSRenderDetail::MeshFrameBindingSnapshot m_frameBindings;
    Array<u8, sizeof(f32) * NWB_MESH_VIEW_FLOAT_COUNT> m_viewBytes{};
    Core::GpuTaskId m_producer;
    b8 m_captured = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

