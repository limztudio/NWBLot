// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_transparent_csg_tasks.h"

#include <impl/ecs_render/material/task_graph_object_geometry_cache.h>
#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/mesh/renderer_mesh_types.h>
#include <impl/ecs_render/csg/graph_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<FrameGraphTransparentCsgTaskResult> FrameGraphTransparentCsgTasks::declare(
    Core::Alloc::ScratchArena& scratchArena,
    const FrameGraphTransparentCsgTaskInputs& inputs,
    ObjectGeometryCacheGraph& objectGeometry,
    RendererTaskGraphDetail::AvboitPreGraphTask::Payload& prePayload,
    ECSRenderDetail::AvboitCsgReceiverSpanGraphTask::Payload& receiverSpanPayload,
    ECSRenderDetail::AvboitCsgIntervalCombineGraphTask::Payload& intervalCombinePayload
){
    FrameGraphTransparentCsgTaskResult result{};
    using namespace RendererTaskGraphDetail;
    Core::GpuTaskId intervalDependency = inputs.transparentCsgUploadTask;

    // Declare interval-producer states here, before native recording, not on the occupancy task.
    const Core::BufferRange transparentCsgInstanceRange(0u, prePayload.transparentCsgSnapshot.instanceCount * sizeof(InstanceGpuData));
    const Core::BufferRange transparentCsgMaterialTypedRange(0u, prePayload.transparentCsgSnapshot.materialTypedByteCount);
    const Core::BufferRange transparentCsgReceiverRange(0u, prePayload.transparentCsgSnapshot.csgReceiverRanges.size() * sizeof(CsgReceiverRangeGpuData));
    const Core::BufferRange transparentCsgCutterRange(0u, prePayload.transparentCsgSnapshot.csgCutters.size() * sizeof(CsgCutterGpuData));

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitIntervalResourceUses{ scratchArena };
    avboitIntervalResourceUses.reserve(s_TransparentCsgIntervalResourceUseCapacity);
    if(prePayload.transparentCsgStreamsUploaded){
        // The receiver raster binds the framebuffer with LOAD, writes off; declare every attachment to preserve contents.
        avboitIntervalResourceUses.push_back(ReadUse(inputs.albedo, Core::ResourceStates::RenderTarget));
        avboitIntervalResourceUses.push_back(ReadUse(inputs.normal, Core::ResourceStates::RenderTarget));
        avboitIntervalResourceUses.push_back(ReadUse(inputs.worldPosition, Core::ResourceStates::RenderTarget));
        avboitIntervalResourceUses.push_back(ReadUse(inputs.specularRoughness, Core::ResourceStates::RenderTarget));
        avboitIntervalResourceUses.push_back(ReadUse(inputs.depth, Core::ResourceStates::DepthWrite));
        avboitIntervalResourceUses.push_back(ReadUse(inputs.meshView, Core::ResourceStates::ConstantBuffer));
        avboitIntervalResourceUses.push_back(ReadBufferUse(inputs.materialInstances, transparentCsgInstanceRange));
        avboitIntervalResourceUses.push_back(ReadBufferUse(inputs.materialTyped, transparentCsgMaterialTypedRange));
        avboitIntervalResourceUses.push_back(ReadBufferUse(inputs.csgReceiverRanges, transparentCsgReceiverRange));
        avboitIntervalResourceUses.push_back(ReadBufferUse(inputs.csgCutters, transparentCsgCutterRange));
        avboitIntervalResourceUses.push_back(ReadUse(inputs.csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
        avboitIntervalResourceUses.push_back(ReadUse(inputs.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        // Sparse payloads are write-only from Unknown; ID/count stay ReadWrite from their clears.
        avboitIntervalResourceUses.push_back(WriteTextureUse(inputs.csgCapBackNormal, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        avboitIntervalResourceUses.push_back(WriteTextureUse(inputs.csgIntervalDepth, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        avboitIntervalResourceUses.push_back(ReadWriteTextureUse(inputs.csgIntervalId, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess));
        avboitIntervalResourceUses.push_back(WriteTextureUse(inputs.csgReceiverEventData, inputs.csgReceiverEventDataSubresources, Core::ResourceStates::UnorderedAccess));
        avboitIntervalResourceUses.push_back(ReadWriteTextureUse(inputs.csgReceiverEventCount, inputs.csgReceiverEventCountSubresources, Core::ResourceStates::UnorderedAccess));
    }
    const Core::GpuTaskResourceSetUse intervalMaterialGeometrySetUse{ .resourceSet = inputs.transparentCsgMaterialGeometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    const Core::GpuTaskResourceSetUse intervalMaterialSampledTextureSetUse{ .resourceSet = inputs.transparentCsgMaterialSampledTextureSet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    Array<Core::GpuTaskResourceSetUse, s_TransparentCsgMaterialResourceSetUseCapacity> transparentCsgMaterialResourceSetUses = {};
    usize transparentCsgMaterialResourceSetUseCount = 0u;
    if(prePayload.transparentCsgMaterialGeometryStatesGraphOwned)
        transparentCsgMaterialResourceSetUses[transparentCsgMaterialResourceSetUseCount++] = intervalMaterialGeometrySetUse;
    if(inputs.transparentCsgMaterialSampledTextureSet.valid())
        transparentCsgMaterialResourceSetUses[transparentCsgMaterialResourceSetUseCount++] = intervalMaterialSampledTextureSetUse;
    avboitIntervalResourceUses.push_back(ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    avboitIntervalResourceUses.push_back(ReadUse(inputs.avboitMaterialDomain));
    avboitIntervalResourceUses.push_back(ReadWriteUse(inputs.avboitCsgDomain, Core::ResourceStates::ShaderResource));

    if(prePayload.transparentCsgStreamsUploaded && !objectGeometry.prepare(
        prePayload.transparentCsgSnapshot.receiverSurfaceIndexedDrawItems,
        prePayload.frameBindings, scratchArena, intervalDependency, avboitIntervalResourceUses,
        inputs.timingTicket
    ))
        return MakeUnexpected(Failure{});

    Core::GpuTaskSchedulingHint avboitIntervalScheduling;
    avboitIntervalScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitIntervalScheduling.forceSubmissionBoundary = false;
    avboitIntervalScheduling.allowPacketMerge = true;
    avboitIntervalScheduling.mergeWithPrevious = prePayload.transparentCsgStreamsUploaded;
    Core::GpuTaskDesc avboitIntervalDesc;
    avboitIntervalDesc
        .setIdentity(Name(s_RenderNameRenderAvboitIntervals))
        .setMarkerLabel(RendererGpuPassText::s_TransparentCsgIntervalsLabel)
        .setScheduling(avboitIntervalScheduling)
        .setDependencies({ &intervalDependency, 1u })
        .setResourceUses(avboitIntervalResourceUses)
        .setResourceSetUses({ transparentCsgMaterialResourceSetUseCount != 0u ? transparentCsgMaterialResourceSetUses.data() : nullptr, transparentCsgMaterialResourceSetUseCount })
    ;
    const b8 avboitCsgReceiverSpanGraphOwned =
        prePayload.transparentCsgStreamsUploaded
        && prePayload.transparentCsgSnapshot.captured
        && receiverSpanPayload.transparentCsgSnapshot.captured
        && receiverSpanPayload.csgFrameBuffersUploaded
    ;
    const b8 avboitCsgIntervalCombineGraphOwned = avboitCsgReceiverSpanGraphOwned && intervalCombinePayload.transparentCsgSnapshot.captured && intervalCombinePayload.csgFrameBuffersUploaded;
    const auto preTaskResult = m_graph.addTask<AvboitPreGraphTask>(avboitIntervalDesc, Move(prePayload));
    if(!preTaskResult){
        m_avboitSystem.taskGraphStage().m_preTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval graph task"));
        return MakeUnexpected(Failure{});
    }
    m_avboitSystem.taskGraphStage().m_preTask = *preTaskResult;

    Core::GpuTaskId avboitIntervalCompletionTask = m_avboitSystem.taskGraphStage().m_preTask;
    b8 avboitIntervalOutputsGraphOwned = false;
    if(avboitCsgReceiverSpanGraphOwned){
        const Array<Core::GpuTaskResourceUse, 6u> avboitIntervalSpanResourceUses = {
            ReadTextureUse(inputs.csgReceiverEventData, inputs.csgReceiverEventDataSubresources, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(inputs.csgReceiverEventCount, inputs.csgReceiverEventCountSubresources, Core::ResourceStates::UnorderedAccess),
            ReadUse(inputs.csgClipContextSlots, Core::ResourceStates::ConstantBuffer),
            ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
            WriteTextureUse(inputs.csgReceiverSpanData, inputs.csgReceiverSpanDataSubresources, Core::ResourceStates::UnorderedAccess),
            WriteTextureUse(inputs.csgReceiverSpanCount, inputs.csgReceiverSpanCountSubresources, Core::ResourceStates::UnorderedAccess),
        };
        Core::GpuTaskSchedulingHint avboitIntervalSpanScheduling;
        avboitIntervalSpanScheduling.cost = Core::GpuTaskCostHint::Medium;
        avboitIntervalSpanScheduling.forceSubmissionBoundary = false;
        avboitIntervalSpanScheduling.allowPacketMerge = true;
        avboitIntervalSpanScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc avboitIntervalSpanDesc;
        avboitIntervalSpanDesc
            .setIdentity(Name(s_RenderNameRenderAvboitTransparentCsgReceiverSpan))
            .setMarkerLabel("Transparent CSG Receiver Span")
            .setScheduling(avboitIntervalSpanScheduling)
            .setDependencies({ &m_avboitSystem.taskGraphStage().m_preTask, 1u })
            .setResourceUses(avboitIntervalSpanResourceUses)
        ;
        const auto csgReceiverSpanTaskResult = m_graph.addTask< ECSRenderDetail::AvboitCsgReceiverSpanGraphTask >(avboitIntervalSpanDesc, Move(receiverSpanPayload));
        if(!csgReceiverSpanTaskResult){
            m_avboitSystem.taskGraphStage().m_csgReceiverSpanTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG receiver-span graph task"));
            return MakeUnexpected(Failure{});
        }
        m_avboitSystem.taskGraphStage().m_csgReceiverSpanTask = *csgReceiverSpanTaskResult;
        avboitIntervalCompletionTask = m_avboitSystem.taskGraphStage().m_csgReceiverSpanTask;
    }
    if(avboitCsgIntervalCombineGraphOwned){
        const Array<Core::GpuTaskResourceUse, 11u> avboitIntervalCombineResourceUses = {
            ReadTextureUse(inputs.csgCapBackNormal, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(inputs.csgIntervalDepth, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(inputs.csgIntervalId, inputs.csgPeelSubresources, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(inputs.csgReceiverSpanData, inputs.csgReceiverSpanDataSubresources, Core::ResourceStates::UnorderedAccess),
            ReadTextureUse(inputs.csgReceiverSpanCount, inputs.csgReceiverSpanCountSubresources, Core::ResourceStates::UnorderedAccess),
            ReadUse(inputs.csgClipContextSlots, Core::ResourceStates::ConstantBuffer),
            ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
            WriteTextureUse(inputs.csgRemovedIntervalDepth, inputs.csgRemovedIntervalSubresources, Core::ResourceStates::UnorderedAccess),
            WriteTextureUse(inputs.csgRemovedIntervalCapNormal, inputs.csgRemovedIntervalSubresources, Core::ResourceStates::UnorderedAccess),
            WriteTextureUse(inputs.csgRemovedIntervalData, inputs.csgRemovedIntervalSubresources, Core::ResourceStates::UnorderedAccess),
            WriteTextureUse(inputs.csgRemovedIntervalCount, inputs.csgRemovedIntervalCountSubresources, Core::ResourceStates::UnorderedAccess),
        };
        Core::GpuTaskSchedulingHint avboitIntervalCombineScheduling;
        avboitIntervalCombineScheduling.cost = Core::GpuTaskCostHint::Medium;
        avboitIntervalCombineScheduling.forceSubmissionBoundary = false;
        avboitIntervalCombineScheduling.allowPacketMerge = true;
        avboitIntervalCombineScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc avboitIntervalCombineDesc;
        avboitIntervalCombineDesc
            .setIdentity(Name(s_RenderNameRenderAvboitTransparentCsgIntervalCombine))
            .setMarkerLabel("Transparent CSG Interval Combine")
            .setScheduling(avboitIntervalCombineScheduling)
            .setDependencies({ &avboitIntervalCompletionTask, 1u })
            .setResourceUses(avboitIntervalCombineResourceUses)
        ;
        const auto csgIntervalCombineTaskResult = m_graph.addTask< ECSRenderDetail::AvboitCsgIntervalCombineGraphTask >(avboitIntervalCombineDesc, Move(intervalCombinePayload));
        if(!csgIntervalCombineTaskResult){
            m_avboitSystem.taskGraphStage().m_csgIntervalCombineTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval-combine graph task"));
            return MakeUnexpected(Failure{});
        }
        m_avboitSystem.taskGraphStage().m_csgIntervalCombineTask = *csgIntervalCombineTaskResult;
        avboitIntervalCompletionTask = m_avboitSystem.taskGraphStage().m_csgIntervalCombineTask;
        avboitIntervalOutputsGraphOwned = true;
    }
    result.intervalCompletionTask = avboitIntervalCompletionTask;
    result.intervalOutputsGraphOwned = avboitIntervalOutputsGraphOwned;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

