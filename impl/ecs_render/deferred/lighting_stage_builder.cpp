// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/deferred/lighting_stage_builder.h>


#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DeferredLightingStageBuilder::DeferredLightingStageBuilder(
    Core::GpuTaskGraph& graph,
    RendererDeferredSystem& deferredSystem
)
    : m_graph(graph)
    , m_deferredSystem(deferredSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool DeferredLightingStageBuilder::declare(
    const DeferredLightingStageInputs& inputs,
    Core::GpuTaskId& outUploadTask,
    Core::GpuTimingSubmissionTicket& timingTicket,
    DeferredLightingStageResult& outResult
){
    using namespace RendererTaskGraphDetail;
    outResult = DeferredLightingStageResult{};
    outUploadTask = Core::GpuTaskId{};
    if(
        !inputs.targets
        || !inputs.albedo.valid()
        || !inputs.normal.valid()
        || !inputs.worldPosition.valid()
        || !inputs.depth.valid()
        || !inputs.shadowVisibility.valid()
        || !inputs.causticIrradiance.valid()
        || !inputs.surfelIrradiance.valid()
        || !inputs.sceneShading.valid()
        || !inputs.lights.valid()
        || !inputs.bindlessSlots.valid()
        || !inputs.opaqueColor.valid()
        || !inputs.graphicsPrefixTask.valid()
        || !inputs.avboitFinalTask.valid()
        || !inputs.shadowVisibilityTask.valid()
        || !inputs.surfelGiTask.valid()
        || (inputs.declaresHardwareCaustics && !inputs.hardwareCausticsTask.valid())
        || (!inputs.declaresHardwareCaustics && !inputs.softwareCausticsTask.valid())
    )
        return false;

    DeferredFrameTargets& deferredTargets = *inputs.targets;
    const bool useLaggedLightingHistory = inputs.useLaggedLightingHistory;
    const DeferredLaggedLightingHistoryResources* const history = inputs.history;
    if(useLaggedLightingHistory && !history)
        return false;
    const bool hasTransparentRenderers = inputs.hasTransparentRenderers;

    // Live Lighting joins current producers via graph edges; lagged Lighting reads history.
    const Core::GpuTaskId hardwareLightingDependencies[] = {
        inputs.shadowVisibilityTask,
        inputs.surfelGiTask,
        inputs.avboitFinalTask,
        inputs.hardwareCausticsTask,
    };
    const Core::GpuTaskId softwareLightingDependencies[] = {
        inputs.shadowVisibilityTask,
        inputs.softwareCausticsTask,
        inputs.surfelGiTask,
        inputs.avboitFinalTask,
    };
    // Lagged Lighting reads prefix inputs plus history; finalizer restores depth layout first.
    const Core::GpuTaskId laggedLightingDependencies[] = {
        inputs.graphicsPrefixTask,
        inputs.avboitFinalTask,
    };
    const usize laggedLightingDependencyCount = hasTransparentRenderers ? 2u : 1u;
    const Core::GpuTaskId laggedLightingSelectorUploadDependencies[] = { inputs.graphicsPrefixTask };
    const bool laggedBindlessSlotsGraphOwned = useLaggedLightingHistory && !history->slotsUploaded;
    Core::GpuTaskId historySlotsUploadTask;
    if(laggedBindlessSlotsGraphOwned){
        const Core::GpuUploadBlobId laggedBindlessSlotsBlob = m_graph.copyUploadData(
            &history->slots,
            sizeof(history->slots),
            alignof(DeferredBindlessResourceSlots)
        );
        if(!laggedBindlessSlotsBlob.valid())
            return false;

        Core::GpuTaskSchedulingHint uploadScheduling;
        uploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
        uploadScheduling.forceSubmissionBoundary = false;
        uploadScheduling.allowPacketMerge = true;
        Core::GpuTaskDesc uploadDesc;
        uploadDesc
            .setIdentity(Name("render.lagged_lighting.bindless_slots_upload"))
            .setMarkerLabel("Lagged Lighting Bindless Slots Upload")
            .setQueue(ComputeUploadQueueRequest())
            .setScheduling(uploadScheduling)
            .setDependencies(
                laggedLightingSelectorUploadDependencies,
                LengthOf(laggedLightingSelectorUploadDependencies)
            )
        ;
        historySlotsUploadTask = m_graph.addUploadBufferTask(
            uploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = laggedBindlessSlotsBlob,
                .destination = inputs.bindlessSlots,
                // Automatic-state selector buffers publish Common; Deferred Lighting owns the following
                // ConstantBuffer transition in this same externally gated packet.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!historySlotsUploadTask.valid())
            return false;
    }
    outUploadTask = historySlotsUploadTask;
    const Core::GpuTaskId laggedLightingWithSelectorDependencies[] = {
        inputs.graphicsPrefixTask,
        historySlotsUploadTask,
        inputs.avboitFinalTask,
    };
    const Core::GpuTaskId* const lightingDependencies = inputs.declaresHardwareCaustics
        ? (useLaggedLightingHistory ? laggedLightingDependencies : hardwareLightingDependencies)
        : (useLaggedLightingHistory ? laggedLightingDependencies : softwareLightingDependencies)
    ;
    const Core::GpuTaskId* const resolvedLightingDependencies = laggedBindlessSlotsGraphOwned
        ? laggedLightingWithSelectorDependencies
        : lightingDependencies
    ;
    const usize lightingDependencyCount = laggedBindlessSlotsGraphOwned
        ? (hasTransparentRenderers ? 3u : 2u)
        : (useLaggedLightingHistory
            ? laggedLightingDependencyCount
            : LengthOf(hardwareLightingDependencies))
    ;

    const Core::GpuExternalCompletionId laggedLightingExternalDependencies[] = {
        inputs.historyReadReadyCompletion,
    };
    const Core::GpuExternalCompletionId* const lightingExternalDependencies = useLaggedLightingHistory
        ? laggedLightingExternalDependencies
        : nullptr
    ;
    const usize lightingExternalDependencyCount = useLaggedLightingHistory
        ? LengthOf(laggedLightingExternalDependencies)
        : 0u
    ;

    // Active lagged Lighting receives compiler-owned state seeds for its shared prefix inputs while it reads
    // history. Transparent depth is the explicit exception: the finalizer dependency above orders its temporary
    // AVBOIT DepthRead layout before Lighting samples ShaderResource state.
    const Core::GpuTaskResourceUse resourceUses[] = {
        ReadTextureUse(
            inputs.albedo,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ),
        ReadTextureUse(
            inputs.normal,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ),
        ReadTextureUse(
            inputs.worldPosition,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ),
        ReadTextureUse(
            inputs.depth,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        ),
        ReadTextureUse(inputs.shadowVisibility, ECSRenderDetail::s_ShadowVisibilitySubresources),
        ReadTextureUse(inputs.causticIrradiance, ECSRenderDetail::s_FramebufferSubresources),
        ReadTextureUse(inputs.surfelIrradiance, ECSRenderDetail::s_FramebufferSubresources),
        ReadUse(
            inputs.sceneShading,
            Core::ResourceStates::ConstantBuffer
        ),
        ReadUse(inputs.lights, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.bindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteTextureUse(inputs.opaqueColor, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess),
    };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = !laggedBindlessSlotsGraphOwned;
    scheduling.allowPacketMerge = laggedBindlessSlotsGraphOwned;
    scheduling.mergeWithPrevious = laggedBindlessSlotsGraphOwned;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.deferred_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(resolvedLightingDependencies, lightingDependencyCount)
        .setExternalDependencies(lightingExternalDependencies, lightingExternalDependencyCount)
        .setResourceUses(resourceUses, LengthOf(resourceUses))
    ;
    outResult.lightingTask = m_deferredSystem.declareDeferredLightingTask(
        m_graph,
        desc,
        deferredTargets,
        useLaggedLightingHistory,
        timingTicket
    );
    if(!outResult.lightingTask.valid())
        return false;
    outResult.historySlotsUploadTask = historySlotsUploadTask;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
