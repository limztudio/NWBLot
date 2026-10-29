// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMaterialSystem;
struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Pass traits select timing, pipeline and framebuffer for the common alias-free/CSG record sequence.
struct AvboitComputeEmulationRecordTrait{
    const Core::GpuTimingScopeDefinition& timingScope;
    MaterialPipelinePass::Enum pipelinePass = MaterialPipelinePass::AvboitOccupancy;
    Core::FramebufferHandle AvboitFrameTargets::* framebuffer = nullptr;
};

struct AvboitComputeEmulationRecordInputs{
    Core::GraphicsRuntime& graphics;
    RendererMaterialSystem& materialSystem;
    DeferredFrameTargets& targets;
    Core::GpuTimingSubmissionTicket& timingTicket;
    Optional<Core::GpuTimingMeasure>& timing;
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings;
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources;
    const ECSRenderDetail::AvboitAliasFreeComputeEmulationGraphPlan& plan;
    const ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphPlan& csgPlan;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    b8 materialDrawBuffersUploaded = false;
    b8 csgFrameBuffersUploaded = false;
    b8 materialFrameStatesGraphOwned = false;
    b8 materialGeometryStatesGraphOwned = false;
    b8 conservativeGeometryScissor = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 RecordAvboitComputeEmulation(const AvboitComputeEmulationRecordInputs& inputs, Core::CommandList& commandList, const AvboitComputeEmulationRecordTrait& trait);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AvboitSharedComputeEmulationPhase{
    enum Enum : u8{
        Generate,
        Raster,
    };
};

struct AvboitSharedComputeEmulationRecordInputs{
    Core::GraphicsRuntime& graphics;
    RendererMaterialSystem& materialSystem;
    DeferredFrameTargets& targets;
    Core::GpuTimingSubmissionTicket& timingTicket;
    Optional<Core::GpuTimingMeasure>& timing;
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings;
    const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan& plan;
    usize drawIndex = 0u;
    usize instanceCount = 0u;
    usize materialTypedByteCount = 0u;
    b8 materialDrawBuffersUploaded = false;
    b8 materialFrameStatesGraphOwned = false;
    b8 materialGeometryStatesGraphOwned = false;
    b8 beginTiming = false;
    b8 finishTiming = false;
    AvboitSharedComputeEmulationPhase::Enum phase = AvboitSharedComputeEmulationPhase::Generate;
};

using AvboitSharedComputeEmulationRecordTrait = AvboitComputeEmulationRecordTrait;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 RecordAvboitSharedComputeEmulation(const AvboitSharedComputeEmulationRecordInputs& inputs, Core::CommandList& commandList, const AvboitSharedComputeEmulationRecordTrait& trait);

template<typename PayloadT>
[[nodiscard]] inline b8 RecordAvboitComputeEmulationFromPayload(const PayloadT& payload, Core::CommandList& commandList, const AvboitComputeEmulationRecordTrait& trait, Optional<Core::GpuTimingMeasure>& timing){
    const AvboitComputeEmulationRecordInputs inputs{
        payload.graphics,
        payload.materialSystem,
        payload.targets,
        payload.timingTicket,
        timing,
        payload.frameBindings,
        payload.csgResources,
        payload.plan,
        payload.csgPlan,
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
[[nodiscard]] inline b8 RecordAvboitRasterPassFromPayload(
    const PayloadT& payload,
    Core::CommandList& commandList,
    const b8 PayloadT::* phasePreparedMember,
    const ECSRenderDetail::TransparentMaterialPassGraphSnapshot PayloadT::* snapshotMember,
    const b8 PayloadT::* emuOutputStatesMember,
    const b8 PayloadT::* csgEmuOutputStatesMember,
    DispatchFn&& dispatch,
    Optional<Core::GpuTimingMeasure>* timing
){
    if(((payload.*emuOutputStatesMember || payload.*csgEmuOutputStatesMember) && !payload.generatedGeometryReused && !timing))
        return false;

    Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(payload.timingTicket);
    MaterialPassDrawItemPartitionsView drawItems{};
    CsgFrameGpuDataView csgFrameData{};
    usize preparedInstanceCount = 0u;
    usize preparedMaterialTypedByteCount = 0u;
    if(payload.hasTransparentRenderers && (!(payload.*phasePreparedMember) || !(payload.*snapshotMember).captured))
        return false;
    if((payload.*phasePreparedMember) && (payload.*snapshotMember).captured){
        drawItems = (payload.*snapshotMember).drawItemsView();
        csgFrameData = (payload.*snapshotMember).csgFrameDataView();
        preparedInstanceCount = (payload.*snapshotMember).instanceCount;
        preparedMaterialTypedByteCount = (payload.*snapshotMember).materialTypedByteCount;
    }
    if(payload.hasTransparentRenderers){
        dispatch(commandList, drawItems, csgFrameData, preparedInstanceCount, preparedMaterialTypedByteCount);
    }
    return true;
}

template<typename PayloadT>
[[nodiscard]] inline b8 RecordAvboitSharedComputeEmulationFromPayload(const PayloadT& payload, Core::CommandList& commandList, const b8 isRasterPhase, const AvboitSharedComputeEmulationRecordTrait& trait, Optional<Core::GpuTimingMeasure>& timing){
    const AvboitSharedComputeEmulationRecordInputs inputs{
        payload.graphics,
        payload.materialSystem,
        payload.targets,
        payload.timingTicket,
        timing,
        payload.frameBindings,
        payload.plan,
        payload.drawIndex,
        payload.instanceCount,
        payload.materialTypedByteCount,
        payload.materialDrawBuffersUploaded,
        payload.materialFrameStatesGraphOwned,
        payload.materialGeometryStatesGraphOwned,
        payload.beginTiming,
        payload.finishTiming,
        isRasterPhase ? AvboitSharedComputeEmulationPhase::Raster : AvboitSharedComputeEmulationPhase::Generate,
    };
    return RecordAvboitSharedComputeEmulation(inputs, commandList, trait);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

