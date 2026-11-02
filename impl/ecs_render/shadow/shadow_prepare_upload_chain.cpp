// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_prepare_upload_chain.h"

#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/graphics/runtime/runtime.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shadow_prepare_upload_chain{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<Core::GpuGraphResourceId> ImportOptionalExternalBuffer(
    Core::GpuTaskGraph& graph,
    const Core::BufferHandle& buffer,
    const Name identity,
    const AStringView label
){
    if(!buffer)
        return Core::GpuGraphResourceId{};
    return RendererTaskGraphDetail::ImportExternalBuffer(graph, buffer, identity, label);
}

[[nodiscard]] inline b8 AppendChainedUploadBufferTask(
    Core::GpuTaskGraph& graph,
    const Name identity,
    const AStringView markerLabel,
    const Core::GpuTaskCostHint::Enum cost,
    const Core::GpuUploadBlobId source,
    const Core::GpuGraphResourceId destination,
    const tchar* const warningMessage,
    Core::GpuTaskId& inOutDependencyTask,
    Core::GpuTaskId& outTask
){
    Core::GpuTaskSchedulingHint uploadScheduling;
    uploadScheduling.cost = cost;
    uploadScheduling.forceSubmissionBoundary = false;
    uploadScheduling.allowPacketMerge = true;
    uploadScheduling.mergeWithPrevious = true;

    Core::GpuTaskDesc uploadDesc;
    uploadDesc.setIdentity(identity).setMarkerLabel(markerLabel).setScheduling(uploadScheduling).setDependencies({ &inOutDependencyTask, 1u });
    const auto uploadTaskResult = graph.addUploadBufferTask(
        uploadDesc,
        Core::GpuUploadBufferTaskDesc{ .source = source, .destination = destination, .finalState = Core::ResourceStates::Common, }
    );
    if(!uploadTaskResult){
        outTask = {};
        NWB_LOGGER_WARNING(warningMessage);
        return false;
    }
    outTask = *uploadTaskResult;
    inOutDependencyTask = outTask;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowPrepareUploadChainBuilder::declareSelectorAndContextUploads(const ShadowPrepareUploadInputs& inputs, Core::Alloc::ScratchArena& scratchArena, ShadowPrepareUploadChain& chain){
    using namespace RendererTaskGraphDetail;

    chain.currentBindlessSlotsGraphOwned = !inputs.targets.bindless.slotsUploaded;
    if(chain.currentBindlessSlotsGraphOwned){
        const auto bindlessSlotsBlobResult = m_graph.copyUploadData(&inputs.targets.bindless.slots, sizeof(inputs.targets.bindless.slots), alignof(DeferredBindlessResourceSlots));
        if(!bindlessSlotsBlobResult){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain deferred bindless selector upload data"));
            return false;
        }
        const Core::GpuUploadBlobId bindlessSlotsBlob = *bindlessSlotsBlobResult;

        Core::GpuTaskSchedulingHint uploadScheduling;
        uploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
        uploadScheduling.forceSubmissionBoundary = false;
        uploadScheduling.allowPacketMerge = true;
        Core::GpuTaskDesc uploadDesc;
        uploadDesc.setIdentity(Name("render.deferred.bindless_slots_upload")).setMarkerLabel("Deferred Bindless Slots Upload").setScheduling(uploadScheduling);
        const auto deferredBindlessSlotsUploadTaskResult = m_graph.addUploadBufferTask(
            uploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = bindlessSlotsBlob,
                .destination = inputs.currentBindlessSlots,
                // Selector persists across packet closes; upload publishes ConstantBuffer.
                .finalState = Core::ResourceStates::ConstantBuffer, }
        );
        if(!deferredBindlessSlotsUploadTaskResult){
            chain.tasks.bindlessSlots = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred bindless selector upload"));
            return false;
        }
        chain.tasks.bindlessSlots = *deferredBindlessSlotsUploadTaskResult;
    }

    // Trace buffers finalized by preflight; retain slots before compile.
    const auto rayTraceMaterialContextSlots = m_raytracingSystem.snapshotRayTraceMaterialContextSlots();
    if(!rayTraceMaterialContextSlots){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not snapshot ray-trace material-context selector"));
        return false;
    }
    const auto rayTraceMaterialContextSlotsBlobResult = m_graph.copyUploadData(&*rayTraceMaterialContextSlots, sizeof(*rayTraceMaterialContextSlots), alignof(RayTraceMaterialContextSlots));
    if(!rayTraceMaterialContextSlotsBlobResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain ray-trace material-context selector upload data"));
        return false;
    }
    const Core::GpuUploadBlobId rayTraceMaterialContextSlotsBlob = *rayTraceMaterialContextSlotsBlobResult;

    Vector<Core::GpuExternalCompletionId, Core::Alloc::ScratchArena> csgReadCompletions{ scratchArena };
    if(!inputs.rayTracingResources.rayTraceContextReadSubmissionTokens.empty()){
        csgReadCompletions.reserve(inputs.rayTracingResources.rayTraceContextReadSubmissionTokens.size());
        for(const auto& token : inputs.rayTracingResources.rayTraceContextReadSubmissionTokens){
            if(!token.valid())
                continue;
            const AString<Core::Alloc::ScratchArena> completionName = StringFormat(scratchArena, "render.raytrace.csg_queue_{}.read_complete", token.physicalQueueIndex);
            Core::GpuExternalCompletionDesc readCompleteDesc;
            const auto completionIdentity = ToName(completionName);
            if(!completionIdentity || !*completionIdentity)
                return false;
            readCompleteDesc.setIdentity(**completionIdentity).setMarkerLabel("Prior CSG Ray Read Complete").setToken(token);
            const auto readCompleteResult = m_graph.importExternalCompletion(readCompleteDesc);
            if(!readCompleteResult)
                return false;
            const Core::GpuExternalCompletionId readComplete = *readCompleteResult;
            csgReadCompletions.push_back(readComplete);
        }
    }

    const Core::GpuTaskId* const materialContextUploadDependencies = chain.currentBindlessSlotsGraphOwned ? &chain.tasks.bindlessSlots : nullptr;
    const usize materialContextUploadDependencyCount = chain.currentBindlessSlotsGraphOwned ? 1u : 0u;
    Core::GpuTaskSchedulingHint materialContextUploadScheduling;
    materialContextUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
    materialContextUploadScheduling.forceSubmissionBoundary = false;
    materialContextUploadScheduling.allowPacketMerge = true;
    materialContextUploadScheduling.mergeWithPrevious = chain.currentBindlessSlotsGraphOwned;
    Core::GpuTaskDesc materialContextUploadDesc;
    materialContextUploadDesc
        .setIdentity(Name("render.raytrace.material_context_slots_upload"))
        .setMarkerLabel("Ray-Trace Material Context Slots Upload")
        .setScheduling(materialContextUploadScheduling)
        .setDependencies({ materialContextUploadDependencies, materialContextUploadDependencyCount })
        // Old CSG rays read this persistent selector as well as their retained context buffer.
        .setExternalDependencies(csgReadCompletions)
    ;
    const auto rayTraceMaterialContextSlotsUploadTaskResult = m_graph.addUploadBufferTask(
        materialContextUploadDesc,
        Core::GpuUploadBufferTaskDesc{
            .source = rayTraceMaterialContextSlotsBlob,
            .destination = inputs.materialContextSlots,
            // Selector buffers publish Common; Shadow Preparation owns the next transition.
            .finalState = Core::ResourceStates::Common, }
    );
    if(!rayTraceMaterialContextSlotsUploadTaskResult){
        chain.tasks.rayTraceMaterialContextSlots = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare ray-trace material-context selector upload"));
        return false;
    }
    chain.tasks.rayTraceMaterialContextSlots = *rayTraceMaterialContextSlotsUploadTaskResult;
    const Name causticEmissionTargetsIdentity = m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
            ? Name(s_HardwareCausticEmissionTargetsName)
            : Name(s_SoftwareCausticEmissionTargetsName)
    ;
    const auto causticEmissionTargetsResult = __hidden_shadow_prepare_upload_chain::ImportOptionalExternalBuffer(m_graph, inputs.rayTracingResources.causticEmissionTargetBuffer, causticEmissionTargetsIdentity, s_CausticEmissionTargetsLabel);
    if(!causticEmissionTargetsResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted caustic emission targets"));
        return false;
    }
    chain.causticEmissionTargets = *causticEmissionTargetsResult;

    const auto causticEmissionTargetsBlob = m_raytracingSystem.retainPreparedCausticEmissionTargetUpload(m_graph);
    if(!causticEmissionTargetsBlob){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted caustic emission-target upload data"));
        return false;
    }

    chain.completionTask = chain.tasks.rayTraceMaterialContextSlots;
    Expected<Core::GpuGraphResourceId> csgTraceContextResult = MakeUnexpected(Failure{});
    if(inputs.rayTracingResources.csgTraceContextBuffer){
        Core::GpuGraphResourceDesc contextDesc = BufferResourceDesc(Name("render.raytrace.csg_context"), "Current CSG Ray Context");
        csgTraceContextResult = m_graph.importBuffer(inputs.rayTracingResources.csgTraceContextBuffer, contextDesc);
        if(csgTraceContextResult)
            chain.csgTraceContext = *csgTraceContextResult;
    }
    const auto csgTraceContextBlob = m_raytracingSystem.retainPreparedCsgTraceContextUpload(m_graph);
    if(!csgTraceContextBlob || (inputs.rayTracingResources.csgTraceContextBuffer && !csgTraceContextResult)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: could not retain current CSG ray context"));
        return false;
    }
    if(*csgTraceContextBlob){
        if(!chain.csgTraceContext.valid())
            return false;
        Core::GpuTaskSchedulingHint csgUploadScheduling = materialContextUploadScheduling;
        csgUploadScheduling.cost = Core::GpuTaskCostHint::Medium;
        csgUploadScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc csgUploadDesc;
        csgUploadDesc
            .setIdentity(Name("render.raytrace.csg_context_upload"))
            .setMarkerLabel("Current CSG Ray Context Upload")
            .setScheduling(csgUploadScheduling)
            .setDependencies({ &chain.completionTask, 1u })
            .setExternalDependencies(csgReadCompletions)
        ;
        const auto csgTraceContextUploadTaskResult = m_graph.addUploadBufferTask(csgUploadDesc, Core::GpuUploadBufferTaskDesc{ .source = **csgTraceContextBlob, .destination = chain.csgTraceContext, .finalState = Core::ResourceStates::Common, });
        if(!csgTraceContextUploadTaskResult){
            chain.tasks.csgTraceContext = {};
            return false;
        }
        chain.tasks.csgTraceContext = *csgTraceContextUploadTaskResult;
        chain.completionTask = chain.tasks.csgTraceContext;
    }
    if(*causticEmissionTargetsBlob){
        if(!chain.causticEmissionTargets.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: caustic emission-target upload has no imported destination"));
            return false;
        }

        if(!__hidden_shadow_prepare_upload_chain::AppendChainedUploadBufferTask(
            m_graph,
            Name("render.raytrace.caustic_emission_targets_upload"),
            "Caustic Emission Targets Upload",
            Core::GpuTaskCostHint::Medium,
            **causticEmissionTargetsBlob,
            chain.causticEmissionTargets,
            NWB_TEXT("RendererSystem: could not declare caustic emission-target upload"),
            chain.completionTask,
            chain.tasks.causticEmissionTargets
        ))
            return false;
    }
    return true;
}

b8 ShadowPrepareUploadChainBuilder::declareSurfelFrameConstantsUpload(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain){
    const auto surfelFrameConstantsResult = __hidden_shadow_prepare_upload_chain::ImportOptionalExternalBuffer(m_graph, inputs.rayTracingResources.surfelFrameConstantsBuffer, Name(s_SurfelConstantsName), s_SurfelConstantsLabel);
    if(!surfelFrameConstantsResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted surfel constants"));
        return false;
    }
    chain.surfelFrameConstants = *surfelFrameConstantsResult;

    const auto surfelFrameConstantsBlob = m_raytracingSystem.retainPreparedSurfelFrameConstantsUpload(m_graph, inputs.targets);
    if(!surfelFrameConstantsBlob){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted surfel-frame constants upload data"));
        return false;
    }
    if(*surfelFrameConstantsBlob){
        if(!chain.surfelFrameConstants.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: surfel-frame constants upload has no imported destination"));
            return false;
        }
        if(!__hidden_shadow_prepare_upload_chain::AppendChainedUploadBufferTask(
            m_graph,
            Name("render.surfel_gi.constants_upload"),
            "Surfel Frame Constants Upload",
            Core::GpuTaskCostHint::Tiny,
            **surfelFrameConstantsBlob,
            chain.surfelFrameConstants,
            NWB_TEXT("RendererSystem: could not declare surfel-frame constants upload"),
            chain.completionTask,
            chain.tasks.surfelFrameConstants
        ))
            return false;
    }
    return true;
}

b8 ShadowPrepareUploadChainBuilder::declareShadowMaterialContextUploads(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain){
    const auto shadowInstanceMaterialsResult = __hidden_shadow_prepare_upload_chain::ImportOptionalExternalBuffer(m_graph, inputs.rayTracingResources.shadowInstanceMaterialBuffer, Name(s_SceneShadowInstanceMaterialName), s_SceneShadowInstanceMaterialLabel);
    const auto shadowInstancesResult = __hidden_shadow_prepare_upload_chain::ImportOptionalExternalBuffer(m_graph, inputs.rayTracingResources.shadowInstanceBuffer, Name(s_SceneShadowInstancesName), s_SceneShadowInstancesLabel);
    const auto shadowMaterialTypedResult = __hidden_shadow_prepare_upload_chain::ImportOptionalExternalBuffer(m_graph, inputs.rayTracingResources.shadowMaterialTypedBuffer, Name(s_SceneShadowMaterialTypedName), s_SceneShadowMaterialTypedLabel);
    if(!shadowInstanceMaterialsResult || !shadowInstancesResult || !shadowMaterialTypedResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted shadow material-context buffers"));
        return false;
    }
    chain.shadowMaterialTyped = *shadowMaterialTypedResult;
    chain.shadowInstances = *shadowInstancesResult;
    chain.shadowInstanceMaterials = *shadowInstanceMaterialsResult;

    const auto shadowMaterialContextUploads = m_raytracingSystem.retainPreparedShadowMaterialContextUploads(m_graph);
    if(!shadowMaterialContextUploads){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted shadow material-context upload data"));
        return false;
    }
    const Core::GpuUploadBlobId shadowInstanceMaterialsBlob = shadowMaterialContextUploads->instanceMaterial;
    const Core::GpuUploadBlobId shadowInstancesBlob = shadowMaterialContextUploads->instance;
    const Core::GpuUploadBlobId shadowMaterialTypedBlob = shadowMaterialContextUploads->materialTyped;
    chain.shadowMaterialContextBatchGraphOwned = shadowInstanceMaterialsBlob.valid();
    if(chain.shadowMaterialContextBatchGraphOwned != shadowInstancesBlob.valid() || chain.shadowMaterialContextBatchGraphOwned != shadowMaterialTypedBlob.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: incomplete frozen shadow material-context upload batch"));
        return false;
    }
    if(chain.shadowMaterialContextBatchGraphOwned){
        if(!chain.shadowInstanceMaterials.valid() || !chain.shadowInstances.valid() || !chain.shadowMaterialTyped.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen shadow material-context batch has no imported destination"));
            return false;
        }
        if(
            !__hidden_shadow_prepare_upload_chain::AppendChainedUploadBufferTask(
                m_graph,
                Name("render.deferred_effects.instance_material_upload"),
                "Shadow Instance Materials Upload",
                Core::GpuTaskCostHint::Medium,
                shadowInstanceMaterialsBlob,
                chain.shadowInstanceMaterials,
                NWB_TEXT("RendererSystem: could not declare shadow instance-material upload"),
                chain.completionTask,
                chain.tasks.shadowInstanceMaterial
            )
            || !__hidden_shadow_prepare_upload_chain::AppendChainedUploadBufferTask(
                m_graph,
                Name("render.deferred_effects.shadow_instances_upload"),
                "Shadow Instances Upload",
                Core::GpuTaskCostHint::Medium,
                shadowInstancesBlob,
                chain.shadowInstances,
                NWB_TEXT("RendererSystem: could not declare shadow instance upload"),
                chain.completionTask,
                chain.tasks.shadowInstance
            )
            || !__hidden_shadow_prepare_upload_chain::AppendChainedUploadBufferTask(
                m_graph,
                Name("render.deferred_effects.material_typed_upload"),
                "Shadow Typed Materials Upload",
                Core::GpuTaskCostHint::Medium,
                shadowMaterialTypedBlob,
                chain.shadowMaterialTyped,
                NWB_TEXT("RendererSystem: could not declare shadow typed-material upload"),
                chain.completionTask,
                chain.tasks.shadowMaterialTyped
            )
        )
            return false;
    }
    return true;
}

b8 ShadowPrepareUploadChainBuilder::declareSceneBvhUploads(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain){
    const auto sceneBvhNodesResult = __hidden_shadow_prepare_upload_chain::ImportOptionalExternalBuffer(m_graph, inputs.rayTracingResources.sceneBvhNodeBuffer, Name(s_SceneBvhNodesName), s_SceneBvhNodesLabel);
    const auto sceneBvhInstancesResult = __hidden_shadow_prepare_upload_chain::ImportOptionalExternalBuffer(m_graph, inputs.rayTracingResources.sceneInstanceBuffer, Name(s_SceneInstancesName), s_SceneInstancesLabel);
    if(!sceneBvhNodesResult || !sceneBvhInstancesResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted software scene-BVH buffers"));
        return false;
    }
    chain.sceneBvhInstances = *sceneBvhInstancesResult;
    chain.sceneBvhNodes = *sceneBvhNodesResult;

    const auto sceneBvhUploads = m_raytracingSystem.retainPreparedSceneBvhUploads(m_graph);
    if(!sceneBvhUploads){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not retain preflighted software scene-BVH upload data"));
        return false;
    }
    const Core::GpuUploadBlobId sceneBvhNodesBlob = sceneBvhUploads->node;
    const Core::GpuUploadBlobId sceneBvhInstancesBlob = sceneBvhUploads->instance;
    chain.sceneBvhBatchGraphOwned = sceneBvhNodesBlob.valid();
    if(chain.sceneBvhBatchGraphOwned != sceneBvhInstancesBlob.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: incomplete frozen software scene-BVH upload pair"));
        return false;
    }
    if(chain.sceneBvhBatchGraphOwned){
        if(!chain.sceneBvhNodes.valid() || !chain.sceneBvhInstances.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen software scene-BVH pair has no imported destination"));
            return false;
        }
        if(
            !__hidden_shadow_prepare_upload_chain::AppendChainedUploadBufferTask(
                m_graph,
                Name("render.shadow_visibility.scene_bvh_nodes_upload"),
                "Scene BVH Nodes Upload",
                Core::GpuTaskCostHint::Medium,
                sceneBvhNodesBlob,
                chain.sceneBvhNodes,
                NWB_TEXT("RendererSystem: could not declare software scene-BVH node upload"),
                chain.completionTask,
                chain.tasks.sceneBvhNodes
            )
            || !__hidden_shadow_prepare_upload_chain::AppendChainedUploadBufferTask(
                m_graph,
                Name("render.shadow_visibility.scene_instances_upload"),
                "Scene BVH Instances Upload",
                Core::GpuTaskCostHint::Medium,
                sceneBvhInstancesBlob,
                chain.sceneBvhInstances,
                NWB_TEXT("RendererSystem: could not declare software scene-BVH instance upload"),
                chain.completionTask,
                chain.tasks.sceneBvhInstances
            )
        )
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

