// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "lighting_stage_builder.h"

#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/shadow/rt_shadow_state.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<DeferredLightingStageResult> DeferredLightingStageBuilder::declare(const DeferredLightingStageInputs& inputs, Core::GpuTimingSubmissionTicket& timingTicket){
    using namespace RendererTaskGraphDetail;
    DeferredLightingStageResult result{};
    if(
        !inputs.albedo.valid()
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
        return MakeUnexpected(Failure{});

    DeferredFrameTargets& deferredTargets = inputs.targets;
    const b8 useLaggedLightingHistory = inputs.useLaggedLightingHistory;
    const DeferredLaggedLightingHistoryResources* const history = inputs.history;
    if(useLaggedLightingHistory && !history)
        return MakeUnexpected(Failure{});
    const b8 hasTransparentRenderers = inputs.hasTransparentRenderers;

    // Live Lighting joins current producers via graph edges; lagged Lighting reads history.
    const Array<Core::GpuTaskId, 4u> hardwareLightingDependencies = { inputs.shadowVisibilityTask, inputs.surfelGiTask, inputs.avboitFinalTask, inputs.hardwareCausticsTask, };
    const Array<Core::GpuTaskId, 4u> softwareLightingDependencies = { inputs.shadowVisibilityTask, inputs.softwareCausticsTask, inputs.surfelGiTask, inputs.avboitFinalTask, };
    // Lagged Lighting reads prefix inputs plus history; finalizer restores depth layout first.
    const Array<Core::GpuTaskId, 2u> laggedLightingDependencies = { inputs.graphicsPrefixTask, inputs.avboitFinalTask, };
    const usize laggedLightingDependencyCount = hasTransparentRenderers ? s_TransparentLaggedLightingDependencyCount : 1u;
    const Array<Core::GpuTaskId, 1u> laggedLightingSelectorUploadDependencies = { inputs.graphicsPrefixTask };
    const b8 laggedBindlessSlotsGraphOwned = useLaggedLightingHistory && !history->slotsUploaded;
    Core::GpuTaskId historySlotsUploadTask;
    if(laggedBindlessSlotsGraphOwned){
        const auto laggedBindlessSlotsBlobResult = m_graph.copyUploadData(&history->slots, sizeof(history->slots), alignof(DeferredBindlessResourceSlots));
        if(!laggedBindlessSlotsBlobResult)
            return MakeUnexpected(Failure{});
        const Core::GpuUploadBlobId laggedBindlessSlotsBlob = *laggedBindlessSlotsBlobResult;

        Core::GpuTaskSchedulingHint uploadScheduling;
        uploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
        uploadScheduling.forceSubmissionBoundary = false;
        uploadScheduling.allowPacketMerge = true;
        Core::GpuTaskDesc uploadDesc;
        uploadDesc
            .setIdentity(Name("render.lagged_lighting.bindless_slots_upload"))
            .setMarkerLabel("Lagged Lighting Bindless Slots Upload")
            .setScheduling(uploadScheduling)
            .setDependencies(laggedLightingSelectorUploadDependencies)
        ;
        const auto historySlotsUploadTaskResult = m_graph.addUploadBufferTask(
            uploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = laggedBindlessSlotsBlob,
                .destination = inputs.bindlessSlots,
                // Selector buffers publish Common; Deferred Lighting owns the ConstantBuffer transition in this same packet.
                .finalState = Core::ResourceStates::Common, }
        );
        if(!historySlotsUploadTaskResult){
            historySlotsUploadTask = {};
            return MakeUnexpected(Failure{});
        }
        historySlotsUploadTask = *historySlotsUploadTaskResult;
    }
    const Array<Core::GpuTaskId, 3u> laggedLightingWithSelectorDependencies = { inputs.graphicsPrefixTask, historySlotsUploadTask, inputs.avboitFinalTask, };
    const Core::GpuTaskId* const lightingDependencies = inputs.declaresHardwareCaustics
        ? (useLaggedLightingHistory ? laggedLightingDependencies.data() : hardwareLightingDependencies.data())
        : (useLaggedLightingHistory ? laggedLightingDependencies.data() : softwareLightingDependencies.data())
    ;
    const Core::GpuTaskId* const resolvedLightingDependencies = laggedBindlessSlotsGraphOwned ? laggedLightingWithSelectorDependencies.data() : lightingDependencies;
    const usize lightingDependencyCount = laggedBindlessSlotsGraphOwned
        ? (hasTransparentRenderers ? s_TransparentLaggedSelectorDependencyCount : s_OpaqueLaggedSelectorDependencyCount)
        : (useLaggedLightingHistory ? laggedLightingDependencyCount : hardwareLightingDependencies.size())
    ;

    const Array<Core::GpuExternalCompletionId, 1u> laggedLightingExternalDependencies = { inputs.historyReadReadyCompletion, };
    const Core::GpuExternalCompletionId* const lightingExternalDependencies = useLaggedLightingHistory ? laggedLightingExternalDependencies.data() : nullptr;
    const usize lightingExternalDependencyCount = useLaggedLightingHistory ? laggedLightingExternalDependencies.size() : 0u;

    // Active lagged Lighting receives compiler-owned state seeds for its shared prefix inputs while it reads history.
    // Transparent depth is the exception: the finalizer above orders its temporary DepthRead layout before Lighting samples it.
    const Array<Core::GpuTaskResourceUse, 11u> resourceUses = {
        ReadTextureUse(inputs.albedo, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(inputs.normal, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(inputs.worldPosition, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(inputs.depth, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource),
        ReadTextureUse(inputs.shadowVisibility, ECSRenderDetail::s_ShadowVisibilitySubresources),
        ReadTextureUse(inputs.causticIrradiance, ECSRenderDetail::s_FramebufferSubresources),
        ReadTextureUse(inputs.surfelIrradiance, ECSRenderDetail::s_FramebufferSubresources),
        ReadUse(inputs.sceneShading, Core::ResourceStates::ConstantBuffer),
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
        .setMarkerLabel(RendererGpuPassText::s_DeferredLightingLabel)
        .setScheduling(scheduling)
        .setDependencies({ resolvedLightingDependencies, lightingDependencyCount })
        .setExternalDependencies({ lightingExternalDependencies, lightingExternalDependencyCount })
        .setResourceUses(resourceUses)
    ;
    const auto lightingTaskResult = m_deferredSystem.declareDeferredLightingTask(m_graph, desc, useLaggedLightingHistory, deferredTargets, timingTicket);
    if(!lightingTaskResult){
        result.lightingTask = {};
        return MakeUnexpected(Failure{});
    }
    result.lightingTask = *lightingTaskResult;
    result.historySlotsUploadTask = historySlotsUploadTask;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

