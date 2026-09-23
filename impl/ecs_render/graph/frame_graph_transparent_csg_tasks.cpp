// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/graph/frame_graph_transparent_csg_tasks.h>

#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/avboit/avboit_system.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/mesh/renderer_mesh_types.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FrameGraphTransparentCsgTasks::FrameGraphTransparentCsgTasks(
    Core::GpuTaskGraph& graph,
    RendererMaterialSystem& materialSystem,
    RendererCsgSystem& csgSystem,
    RendererAvboitSystem& avboitSystem
)
    : m_graph(graph)
    , m_materialSystem(materialSystem)
    , m_csgSystem(csgSystem)
    , m_avboitSystem(avboitSystem){
}


bool FrameGraphTransparentCsgTasks::declare(
    const FrameGraphTransparentCsgTaskInputs& inputs,
    RendererTaskGraphDetail::AvboitPreGraphTask::Payload& prePayload,
    ECSRenderDetail::AvboitCsgReceiverSpanGraphTask::Payload& receiverSpanPayload,
    ECSRenderDetail::AvboitCsgIntervalCombineGraphTask::Payload& intervalCombinePayload,
    FrameGraphTransparentCsgTaskResult& outResult
){
    outResult = FrameGraphTransparentCsgTaskResult{};
    using namespace RendererTaskGraphDetail;
    DeferredFrameTargets& deferredTargets = *inputs.targets;
    const Core::GpuGraphResourceId depth = inputs.depth;
    const Core::GpuGraphResourceId meshView = inputs.meshView;
    const Core::GpuGraphResourceId materialInstances = inputs.materialInstances;
    const Core::GpuGraphResourceId materialTyped = inputs.materialTyped;
    const Core::GpuGraphResourceId csgReceiverRanges = inputs.csgReceiverRanges;
    const Core::GpuGraphResourceId csgCutters = inputs.csgCutters;
    const Core::GpuGraphResourceId csgClipContextSlots = inputs.csgClipContextSlots;
    const Core::GpuGraphResourceId csgIntervalSampleState = inputs.csgIntervalSampleState;
    const Core::GpuGraphResourceId csgCapBackNormal = inputs.csgCapBackNormal;
    const Core::GpuGraphResourceId csgIntervalDepth = inputs.csgIntervalDepth;
    const Core::GpuGraphResourceId csgIntervalId = inputs.csgIntervalId;
    const Core::GpuGraphResourceId csgReceiverEventData = inputs.csgReceiverEventData;
    const Core::GpuGraphResourceId csgReceiverEventCount = inputs.csgReceiverEventCount;
    const Core::GpuGraphResourceId csgReceiverSpanData = inputs.csgReceiverSpanData;
    const Core::GpuGraphResourceId csgReceiverSpanCount = inputs.csgReceiverSpanCount;
    const Core::GpuGraphResourceId csgRemovedIntervalDepth = inputs.csgRemovedIntervalDepth;
    const Core::GpuGraphResourceId csgRemovedIntervalCapNormal = inputs.csgRemovedIntervalCapNormal;
    const Core::GpuGraphResourceId csgRemovedIntervalData = inputs.csgRemovedIntervalData;
    const Core::GpuGraphResourceId csgRemovedIntervalCount = inputs.csgRemovedIntervalCount;
    const Core::GpuGraphResourceId currentBindlessSlots = inputs.currentBindlessSlots;
    const Core::GpuGraphResourceId avboitMaterialDomain = inputs.avboitMaterialDomain;
    const Core::GpuGraphResourceId avboitCsgDomain = inputs.avboitCsgDomain;
    const Core::TextureSubresourceSet csgPeelSubresources = inputs.csgPeelSubresources;
    const Core::TextureSubresourceSet csgReceiverEventDataSubresources = inputs.csgReceiverEventDataSubresources;
    const Core::TextureSubresourceSet csgReceiverEventCountSubresources = inputs.csgReceiverEventCountSubresources;
    const Core::TextureSubresourceSet csgReceiverSpanDataSubresources = inputs.csgReceiverSpanDataSubresources;
    const Core::TextureSubresourceSet csgReceiverSpanCountSubresources = inputs.csgReceiverSpanCountSubresources;
    const Core::TextureSubresourceSet csgRemovedIntervalSubresources = inputs.csgRemovedIntervalSubresources;
    const Core::TextureSubresourceSet csgRemovedIntervalCountSubresources = inputs.csgRemovedIntervalCountSubresources;
    RendererTaskGraphDetail::AvboitPreGraphTask::Payload& avboitPrePayload = prePayload;
    ECSRenderDetail::AvboitCsgReceiverSpanGraphTask::Payload& avboitCsgReceiverSpanPayload = receiverSpanPayload;
    ECSRenderDetail::AvboitCsgIntervalCombineGraphTask::Payload& avboitCsgIntervalCombinePayload = intervalCombinePayload;
    const Core::GpuTaskId intervalUploadTask = inputs.transparentCsgUploadTask;
    const Core::GpuGraphResourceSetId intervalMaterialGeometrySet = inputs.transparentCsgMaterialGeometrySet;
    const Core::GpuGraphResourceSetId intervalMaterialSampledTextureSet = inputs.transparentCsgMaterialSampledTextureSet;

    // Declare interval-producer states here, before native recording, not on the occupancy task.
    const Core::BufferRange transparentCsgInstanceRange(
        0u,
        avboitPrePayload.transparentCsgSnapshot.instanceCount * sizeof(InstanceGpuData)
    );
    const Core::BufferRange transparentCsgMaterialTypedRange(
        0u,
        avboitPrePayload.transparentCsgSnapshot.materialTypedByteCount
    );
    const Core::BufferRange transparentCsgReceiverRange(
        0u,
        avboitPrePayload.transparentCsgSnapshot.csgReceiverRanges.size() * sizeof(CsgReceiverRangeGpuData)
    );
    const Core::BufferRange transparentCsgCutterRange(
        0u,
        avboitPrePayload.transparentCsgSnapshot.csgCutters.size() * sizeof(CsgCutterGpuData)
    );

    Core::Alloc::ScratchArena avboitIntervalResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitIntervalResourceUses{ avboitIntervalResourceScratch };
    avboitIntervalResourceUses.reserve(16u);
    if(avboitPrePayload.transparentCsgStreamsUploaded){
        avboitIntervalResourceUses.push_back(ReadUse(depth));
        avboitIntervalResourceUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
        avboitIntervalResourceUses.push_back(ReadBufferUse(materialInstances, transparentCsgInstanceRange));
        avboitIntervalResourceUses.push_back(ReadBufferUse(materialTyped, transparentCsgMaterialTypedRange));
        avboitIntervalResourceUses.push_back(ReadBufferUse(csgReceiverRanges, transparentCsgReceiverRange));
        avboitIntervalResourceUses.push_back(ReadBufferUse(csgCutters, transparentCsgCutterRange));
        avboitIntervalResourceUses.push_back(ReadUse(csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
        avboitIntervalResourceUses.push_back(ReadUse(csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        // Sparse payloads are write-only from Unknown; ID/count stay ReadWrite from their clears.
        avboitIntervalResourceUses.push_back(
            WriteTextureUse(csgCapBackNormal, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        avboitIntervalResourceUses.push_back(
            WriteTextureUse(csgIntervalDepth, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        avboitIntervalResourceUses.push_back(
            ReadWriteTextureUse(csgIntervalId, csgPeelSubresources, Core::ResourceStates::UnorderedAccess)
        );
        avboitIntervalResourceUses.push_back(WriteTextureUse(
            csgReceiverEventData,
            csgReceiverEventDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalResourceUses.push_back(ReadWriteTextureUse(
            csgReceiverEventCount,
            csgReceiverEventCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
    }
    const Core::GpuTaskResourceSetUse intervalMaterialGeometrySetUse{
        .resourceSet = intervalMaterialGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse intervalMaterialSampledTextureSetUse{
        .resourceSet = intervalMaterialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse transparentCsgMaterialResourceSetUses[2u] = {};
    usize transparentCsgMaterialResourceSetUseCount = 0u;
    if(avboitPrePayload.transparentCsgMaterialGeometryStatesGraphOwned){
        transparentCsgMaterialResourceSetUses[transparentCsgMaterialResourceSetUseCount++] =
            intervalMaterialGeometrySetUse;
    }
    if(intervalMaterialSampledTextureSet.valid()){
        transparentCsgMaterialResourceSetUses[transparentCsgMaterialResourceSetUseCount++] =
            intervalMaterialSampledTextureSetUse;
    }
    avboitIntervalResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    avboitIntervalResourceUses.push_back(ReadUse(avboitMaterialDomain));
    avboitIntervalResourceUses.push_back(ReadWriteUse(avboitCsgDomain, Core::ResourceStates::ShaderResource));

    Core::GpuTaskSchedulingHint avboitIntervalScheduling;
    avboitIntervalScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitIntervalScheduling.forceSubmissionBoundary = false;
    avboitIntervalScheduling.allowPacketMerge = true;
    avboitIntervalScheduling.mergeWithPrevious = avboitPrePayload.transparentCsgStreamsUploaded;
    Core::GpuTaskDesc avboitIntervalDesc;
    avboitIntervalDesc
        .setIdentity(Name("render.avboit.intervals"))
        .setMarkerLabel("Transparent CSG Intervals")
        .setQueue(GraphicsComputeQueueRequest())
        .setScheduling(avboitIntervalScheduling)
        .setDependencies(&intervalUploadTask, 1u)
        .setResourceUses(avboitIntervalResourceUses.data(), avboitIntervalResourceUses.size())
        .setResourceSetUses(
            transparentCsgMaterialResourceSetUseCount != 0u ? transparentCsgMaterialResourceSetUses : nullptr,
            transparentCsgMaterialResourceSetUseCount
        )
    ;
    const bool avboitCsgReceiverSpanGraphOwned =
        avboitPrePayload.transparentCsgStreamsUploaded
        && avboitPrePayload.transparentCsgSnapshot.captured
        && avboitPrePayload.deferTransparentCsgIntervalCombine
        && avboitCsgReceiverSpanPayload.transparentCsgSnapshot.captured
        && avboitCsgReceiverSpanPayload.csgFrameBuffersUploaded
    ;
    const bool avboitCsgIntervalCombineGraphOwned =
        avboitCsgReceiverSpanGraphOwned
        && avboitCsgIntervalCombinePayload.transparentCsgSnapshot.captured
        && avboitCsgIntervalCombinePayload.csgFrameBuffersUploaded
    ;
    Core::Alloc::ScratchArena avboitIntervalSpanResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitIntervalSpanResourceUses{
        avboitIntervalSpanResourceScratch
    };
    Core::Alloc::ScratchArena avboitIntervalCombineResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitIntervalCombineResourceUses{
        avboitIntervalCombineResourceScratch
    };
    if(avboitCsgReceiverSpanGraphOwned){
        avboitIntervalSpanResourceUses.reserve(6u);
        avboitIntervalSpanResourceUses.push_back(ReadTextureUse(
            csgReceiverEventData,
            csgReceiverEventDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalSpanResourceUses.push_back(ReadTextureUse(
            csgReceiverEventCount,
            csgReceiverEventCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalSpanResourceUses.push_back(ReadUse(
            csgClipContextSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        avboitIntervalSpanResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        avboitIntervalSpanResourceUses.push_back(WriteTextureUse(
            csgReceiverSpanData,
            csgReceiverSpanDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalSpanResourceUses.push_back(WriteTextureUse(
            csgReceiverSpanCount,
            csgReceiverSpanCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitCsgReceiverSpanPayload.materialSystem = &m_materialSystem;
        avboitCsgReceiverSpanPayload.csgSystem = &m_csgSystem;
        avboitCsgReceiverSpanPayload.targets = &deferredTargets;
        avboitCsgReceiverSpanPayload.timingTicket = inputs.timingTicket;
        avboitCsgReceiverSpanPayload.transparentCsgIntervalsTiming = inputs.transparentCsgIntervalsTiming;
        avboitCsgReceiverSpanPayload.receiverSpanInputImageStatesGraphOwned = true;
        avboitCsgReceiverSpanPayload.receiverSpanOutputImageStatesGraphOwned = true;
    }
    if(avboitCsgIntervalCombineGraphOwned){
        avboitIntervalCombineResourceUses.reserve(11u);
        avboitIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgCapBackNormal,
            csgPeelSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgIntervalDepth,
            csgPeelSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgIntervalId,
            csgPeelSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgReceiverSpanData,
            csgReceiverSpanDataSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(ReadTextureUse(
            csgReceiverSpanCount,
            csgReceiverSpanCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(ReadUse(
            csgClipContextSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        avboitIntervalCombineResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        avboitIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalDepth,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalCapNormal,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalData,
            csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitIntervalCombineResourceUses.push_back(WriteTextureUse(
            csgRemovedIntervalCount,
            csgRemovedIntervalCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitCsgIntervalCombinePayload.materialSystem = &m_materialSystem;
        avboitCsgIntervalCombinePayload.csgSystem = &m_csgSystem;
        avboitCsgIntervalCombinePayload.targets = &deferredTargets;
        avboitCsgIntervalCombinePayload.timingTicket = inputs.timingTicket;
        avboitCsgIntervalCombinePayload.transparentCsgIntervalsTiming = inputs.transparentCsgIntervalsTiming;
        avboitCsgIntervalCombinePayload.intervalCombineInputImageStatesGraphOwned = true;
        avboitCsgIntervalCombinePayload.removedIntervalOutputImageStatesGraphOwned = true;
    }
    m_avboitSystem.taskGraphStage().m_preTask = m_graph.addTask<AvboitPreGraphTask>(
        avboitIntervalDesc,
        Move(avboitPrePayload)
    );
    if(!m_avboitSystem.taskGraphStage().m_preTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval graph task"));
        return false;
    }

    Core::GpuTaskId avboitIntervalCompletionTask = m_avboitSystem.taskGraphStage().m_preTask;
    bool avboitIntervalOutputsGraphOwned = false;
    if(avboitCsgReceiverSpanGraphOwned){
        Core::GpuTaskSchedulingHint avboitIntervalSpanScheduling;
        avboitIntervalSpanScheduling.cost = Core::GpuTaskCostHint::Medium;
        avboitIntervalSpanScheduling.forceSubmissionBoundary = false;
        avboitIntervalSpanScheduling.allowPacketMerge = true;
        avboitIntervalSpanScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc avboitIntervalSpanDesc;
        avboitIntervalSpanDesc
            .setIdentity(Name("render.avboit.transparent_csg.receiver_span"))
            .setMarkerLabel("Transparent CSG Receiver Span")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(avboitIntervalSpanScheduling)
            .setDependencies(&m_avboitSystem.taskGraphStage().m_preTask, 1u)
            .setResourceUses(
                avboitIntervalSpanResourceUses.data(),
                avboitIntervalSpanResourceUses.size()
            )
        ;
        m_avboitSystem.taskGraphStage().m_csgReceiverSpanTask = m_graph.addTask<
            ECSRenderDetail::AvboitCsgReceiverSpanGraphTask
        >(
            avboitIntervalSpanDesc,
            Move(avboitCsgReceiverSpanPayload)
        );
        if(!m_avboitSystem.taskGraphStage().m_csgReceiverSpanTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG receiver-span graph task"));
            return false;
        }
        avboitIntervalCompletionTask = m_avboitSystem.taskGraphStage().m_csgReceiverSpanTask;
    }
    if(avboitCsgIntervalCombineGraphOwned){
        Core::GpuTaskSchedulingHint avboitIntervalCombineScheduling;
        avboitIntervalCombineScheduling.cost = Core::GpuTaskCostHint::Medium;
        avboitIntervalCombineScheduling.forceSubmissionBoundary = false;
        avboitIntervalCombineScheduling.allowPacketMerge = true;
        avboitIntervalCombineScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc avboitIntervalCombineDesc;
        avboitIntervalCombineDesc
            .setIdentity(Name("render.avboit.transparent_csg.interval_combine"))
            .setMarkerLabel("Transparent CSG Interval Combine")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(avboitIntervalCombineScheduling)
            .setDependencies(&avboitIntervalCompletionTask, 1u)
            .setResourceUses(
                avboitIntervalCombineResourceUses.data(),
                avboitIntervalCombineResourceUses.size()
            )
        ;
        m_avboitSystem.taskGraphStage().m_csgIntervalCombineTask = m_graph.addTask<
            ECSRenderDetail::AvboitCsgIntervalCombineGraphTask
        >(
            avboitIntervalCombineDesc,
            Move(avboitCsgIntervalCombinePayload)
        );
        if(!m_avboitSystem.taskGraphStage().m_csgIntervalCombineTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare transparent CSG interval-combine graph task"));
            return false;
        }
        avboitIntervalCompletionTask = m_avboitSystem.taskGraphStage().m_csgIntervalCombineTask;
        avboitIntervalOutputsGraphOwned = true;
    }
    outResult.intervalCompletionTask = avboitIntervalCompletionTask;
    outResult.intervalOutputsGraphOwned = avboitIntervalOutputsGraphOwned;
    outResult.declared = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

