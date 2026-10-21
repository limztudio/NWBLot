// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "system.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Per-mesh declarations of one skinning frame: the immutable dispatch plan plus its selector upload, rest-to-skinned copy, and joint-palette upload.
// Every declared transfer chains on one serial dependency that the frame's deformation task then follows.
class MeshSkinningSystem::FramePlanBuilder final : NoCopy{
public:
    FramePlanBuilder(MeshSkinningSystem& system, Core::GpuTaskGraph& graph, Core::Alloc::ScratchArena& scratchArena, Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena>& plans)noexcept
        : m_system(system)
        , m_graph(graph)
        , m_scratchArena(scratchArena)
        , m_plans(plans)
    {}


public:
    // False only for a frame-fatal declaration failure; meshes without a ready pose or dirty bounds are skipped.
    [[nodiscard]] bool declare(Core::ECS::EntityID entity, SkinnedMeshBindingComponent& binding);
    [[nodiscard]] const Core::GpuTaskId& terminalTask()const noexcept{ return m_terminalTask; }


private:
    [[nodiscard]] Expected<Core::GpuGraphResourceId> importBuffer(const Core::BufferHandle& buffer, AStringView label);
    [[nodiscard]] Expected<Core::GpuGraphPipelineId> importComputePipeline(const Core::ComputePipelineHandle& pipeline, Name identity, AStringView label);
    [[nodiscard]] bool importPlanResources(const MeshSkinningRuntimeInstance& instance, const RuntimeResources& resources, MeshSkinningGraphDispatchPlan& plan);
    [[nodiscard]] bool declareBindlessSlotsUpload(const MeshSkinningRuntimeInstance& instance, const RuntimeResources& resources, MeshSkinningGraphDispatchPlan& plan);
    [[nodiscard]] bool declareRestToSkinnedCopy(const MeshSkinningRuntimeInstance& instance, const MeshSkinningGraphDispatchPlan& plan);
    [[nodiscard]] bool declareJointPaletteUpload(const MeshSkinningRuntimeInstance& instance, Span<const SkeletonJointMatrix> jointPaletteUpload, const MeshSkinningGraphDispatchPlan& plan);


private:
    MeshSkinningSystem& m_system;
    Core::GpuTaskGraph& m_graph;
    Core::Alloc::ScratchArena& m_scratchArena;
    Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena>& m_plans;
    Core::GpuTaskId m_terminalTask;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

