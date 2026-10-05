// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/avboit/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/csg/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/material/renderer_pipeline_types.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Pass traits select timing, pipeline and framebuffer for the common alias-free/CSG record sequence.
struct AvboitComputeEmulationRecordTrait{
    const Core::GpuTimingScopeDefinition* timingScope = nullptr;
    MaterialPipelinePass::Enum pipelinePass = MaterialPipelinePass::AvboitOccupancy;
    Core::FramebufferHandle AvboitFrameTargets::* framebuffer = nullptr;
};

struct AvboitComputeEmulationRecordInputs{
    Core::GraphicsRuntime* graphics = nullptr;
    RendererMaterialSystem* materialSystem = nullptr;
    DeferredFrameTargets* targets = nullptr;
    Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    Optional<Core::GpuTimingMeasure>* timing = nullptr;
    const ECSRenderDetail::MeshFrameBindingSnapshot* frameBindings = nullptr;
    const ECSRenderDetail::CsgGraphResourceSnapshot* csgResources = nullptr;
    const ECSRenderDetail::AvboitAliasFreeComputeEmulationGraphPlan* plan = nullptr;
    const ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphPlan* csgPlan = nullptr;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    bool materialDrawBuffersUploaded = false;
    bool csgFrameBuffersUploaded = false;
    bool materialFrameStatesGraphOwned = false;
    bool materialGeometryStatesGraphOwned = false;
    bool conservativeGeometryScissor = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool RecordAvboitComputeEmulation(
    const AvboitComputeEmulationRecordInputs& inputs,
    Core::CommandList& commandList,
    const AvboitComputeEmulationRecordTrait& trait
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AvboitSharedComputeEmulationPhase{
    enum Enum : u8{
        Generate,
        Raster,
    };
};

struct AvboitSharedComputeEmulationRecordInputs{
    Core::GraphicsRuntime* graphics = nullptr;
    RendererMaterialSystem* materialSystem = nullptr;
    DeferredFrameTargets* targets = nullptr;
    Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    Optional<Core::GpuTimingMeasure>* timing = nullptr;
    const ECSRenderDetail::MeshFrameBindingSnapshot* frameBindings = nullptr;
    const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan* plan = nullptr;
    usize drawIndex = 0u;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    bool materialDrawBuffersUploaded = false;
    bool materialFrameStatesGraphOwned = false;
    bool materialGeometryStatesGraphOwned = false;
    bool beginTiming = false;
    bool finishTiming = false;
    AvboitSharedComputeEmulationPhase::Enum phase = AvboitSharedComputeEmulationPhase::Generate;
};

struct AvboitSharedComputeEmulationRecordTrait{
    const Core::GpuTimingScopeDefinition* timingScope = nullptr;
    MaterialPipelinePass::Enum pipelinePass = MaterialPipelinePass::AvboitOccupancy;
    Core::FramebufferHandle AvboitFrameTargets::* framebuffer = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool RecordAvboitSharedComputeEmulation(
    const AvboitSharedComputeEmulationRecordInputs& inputs,
    Core::CommandList& commandList,
    const AvboitSharedComputeEmulationRecordTrait& trait
);

template<typename PayloadT>
[[nodiscard]] inline bool RecordAvboitComputeEmulationFromPayload(
    const PayloadT& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context,
    Optional<Core::GpuTimingMeasure>* PayloadT::* timingMember,
    const AvboitComputeEmulationRecordTrait& trait
){
    static_cast<void>(context);
    const AvboitComputeEmulationRecordInputs inputs{
        payload.graphics,
        payload.materialSystem,
        payload.targets,
        payload.timingTicket,
        payload.*timingMember,
        &payload.frameBindings,
        &payload.csgResources,
        &payload.plan,
        &payload.csgPlan,
        payload.instanceCount,
        payload.materialTypedByteCount,
        payload.materialDrawBuffersUploaded,
        payload.csgFrameBuffersUploaded,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        payload.conservativeGeometryScissor,
    };
    return RecordAvboitComputeEmulation(inputs, commandList, trait);
}

template<typename PayloadT, typename DispatchFn>
[[nodiscard]] inline bool RecordAvboitRasterPassFromPayload(
    const PayloadT& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context,
    const bool PayloadT::* phasePreparedMember,
    const ECSRenderDetail::TransparentMaterialPassGraphSnapshot PayloadT::* snapshotMember,
    const bool PayloadT::* emuOutputStatesMember,
    const bool PayloadT::* csgEmuOutputStatesMember,
    Optional<Core::GpuTimingMeasure>* PayloadT::* timingMember,
    DispatchFn&& dispatch
){
    static_cast<void>(context);
    if(
        !payload.avboitSystem
        || !payload.targets
        || !payload.timingTicket
        || ((payload.*emuOutputStatesMember || payload.*csgEmuOutputStatesMember)
            && !payload.generatedGeometryReused && !(payload.*timingMember))
    )
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);
    MaterialPassDrawItemPartitions drawItems{ scratchArena };
    CsgFrameGpuData csgFrameData{ scratchArena };
    usize preparedInstanceCount = 0u;
    usize preparedMaterialTypedByteCount = 0u;
    if(payload.hasTransparentRenderers && (!(payload.*phasePreparedMember) || !(payload.*snapshotMember).captured))
        return false;
    if((payload.*phasePreparedMember) && (payload.*snapshotMember).captured){
        (payload.*snapshotMember).materialize(drawItems, csgFrameData);
        preparedInstanceCount = (payload.*snapshotMember).instanceCount;
        preparedMaterialTypedByteCount = (payload.*snapshotMember).materialTypedByteCount;
    }
    if(payload.hasTransparentRenderers){
        dispatch(
            commandList,
            drawItems,
            csgFrameData,
            preparedInstanceCount,
            preparedMaterialTypedByteCount
        );
    }
    return true;
}

template<typename PayloadT>
[[nodiscard]] inline bool RecordAvboitSharedComputeEmulationFromPayload(
    const PayloadT& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context,
    Optional<Core::GpuTimingMeasure>* PayloadT::* timingMember,
    const bool isRasterPhase,
    const AvboitSharedComputeEmulationRecordTrait& trait
){
    static_cast<void>(context);
    const AvboitSharedComputeEmulationRecordInputs inputs{
        payload.graphics,
        payload.materialSystem,
        payload.targets,
        payload.timingTicket,
        payload.*timingMember,
        &payload.frameBindings,
        &payload.plan,
        payload.drawIndex,
        payload.instanceCount,
        payload.materialTypedByteCount,
        payload.materialDrawBuffersUploaded,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        payload.beginTiming,
        payload.finishTiming,
        isRasterPhase
            ? AvboitSharedComputeEmulationPhase::Raster
            : AvboitSharedComputeEmulationPhase::Generate,
    };
    return RecordAvboitSharedComputeEmulation(inputs, commandList, trait);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

