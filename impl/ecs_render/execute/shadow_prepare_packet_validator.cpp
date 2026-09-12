// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/execute/shadow_prepare_packet_validator.h>


#include <impl/ecs_render/renderer_frame_pipeline.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShadowPreparePacketValidator::ShadowPreparePacketValidator(NotNull<RendererFramePipeline*> pipeline)
    : m_pipeline(pipeline){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ShadowPreparePacketValidator::validate(
    const Core::GpuCompiledGraph::ReadView& compiledPlan,
    ShadowPreparePacketValidationResult& outResult
)const{
    RendererFramePipeline& pipeline = *m_pipeline;
    // Software clears belong to the Shadow Preparation packet; a split would omit them.
    outResult.softwareBvhBuildsMerged =
        !pipeline.m_deferredShadowPrepareSoftwareBvhBuildFirstTask.valid()
            ? !pipeline.m_deferredShadowPrepareSoftwareBvhBuildLastTask.valid()
            : (
                pipeline.m_deferredShadowPrepareSoftwareBvhBuildLastTask.valid()
                && compiledPlan.tasksSharePacket(
                    pipeline.m_deferredShadowPrepareTask,
                    pipeline.m_deferredShadowPrepareSoftwareBvhBuildFirstTask
                )
                && compiledPlan.tasksSharePacket(
                    pipeline.m_deferredShadowPrepareTask,
                    pipeline.m_deferredShadowPrepareSoftwareBvhBuildLastTask
                )
            )
    ;
    // The hybrid tail keeps the old acceptance boundary in this packet.
    outResult.hybridSoftwareTailMerged =
        !pipeline.m_deferredShadowPrepareHybridSoftwareTailTask.valid()
        || compiledPlan.tasksSharePacket(
            pipeline.m_deferredShadowPrepareTask,
            pipeline.m_deferredShadowPrepareHybridSoftwareTailTask
        )
    ;
    // Frozen transitions must share the build's submission for an atomic handoff.
    outResult.accelStructFinalizeMerged =
        !pipeline.m_deferredShadowPrepareAccelStructFinalizeTask.valid()
        || compiledPlan.tasksSharePacket(
            pipeline.m_deferredShadowPrepareTask,
            pipeline.m_deferredShadowPrepareAccelStructFinalizeTask
        )
    ;
    // Selector uploads must stay in the Shadow Preparation packet.
    outResult.bindlessSlotsUploadMerged =
        !pipeline.m_deferredBindlessSlotsUploadTask.valid()
        || compiledPlan.tasksSharePacket(
            pipeline.m_deferredShadowPrepareTask,
            pipeline.m_deferredBindlessSlotsUploadTask
        )
    ;
    // Keep the upload in the first packet so it becomes the handoff.
    outResult.rayTraceMaterialContextSlotsUploadMerged =
        !pipeline.m_rayTraceMaterialContextSlotsUploadTask.valid()
        || compiledPlan.tasksSharePacket(
            pipeline.m_deferredShadowPrepareTask,
            pipeline.m_rayTraceMaterialContextSlotsUploadTask
        )
    ;
    // Nonempty caustic payloads must live in the Shadow Preparation packet.
    outResult.causticEmissionTargetsUploadMerged =
        !pipeline.m_causticEmissionTargetsUploadTask.valid()
        || compiledPlan.tasksSharePacket(
            pipeline.m_deferredShadowPrepareTask,
            pipeline.m_causticEmissionTargetsUploadTask
        )
    ;
    // Surfel constants must share the Shadow Preparation packet.
    outResult.surfelFrameConstantsUploadMerged =
        !pipeline.m_surfelFrameConstantsUploadTask.valid()
        || compiledPlan.tasksSharePacket(
            pipeline.m_deferredShadowPrepareTask,
            pipeline.m_surfelFrameConstantsUploadTask
        )
    ;
    // This triple must stay in the first accepted packet.
    outResult.shadowMaterialContextUploadsMerged =
        (!pipeline.m_shadowInstanceMaterialUploadTask.valid()
            || compiledPlan.tasksSharePacket(
                pipeline.m_deferredShadowPrepareTask,
                pipeline.m_shadowInstanceMaterialUploadTask
            ))
        && (!pipeline.m_shadowInstanceUploadTask.valid()
            || compiledPlan.tasksSharePacket(
                pipeline.m_deferredShadowPrepareTask,
                pipeline.m_shadowInstanceUploadTask
            ))
        && (!pipeline.m_shadowMaterialTypedUploadTask.valid()
            || compiledPlan.tasksSharePacket(
                pipeline.m_deferredShadowPrepareTask,
                pipeline.m_shadowMaterialTypedUploadTask
            ))
    ;
    // Keep the pair in Shadow Preparation as the only producer.
    outResult.sceneBvhUploadsMerged =
        pipeline.m_sceneBvhNodesUploadTask.valid() == pipeline.m_sceneBvhInstancesUploadTask.valid()
        && (!pipeline.m_sceneBvhNodesUploadTask.valid()
            || (
                compiledPlan.tasksSharePacket(
                    pipeline.m_deferredShadowPrepareTask,
                    pipeline.m_sceneBvhNodesUploadTask
                )
                && compiledPlan.tasksSharePacket(
                    pipeline.m_deferredShadowPrepareTask,
                    pipeline.m_sceneBvhInstancesUploadTask
                )
            ))
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
