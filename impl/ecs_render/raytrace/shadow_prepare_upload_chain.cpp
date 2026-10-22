// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_prepare_upload_chain.h"

#include <impl/ecs_render/renderer_frame_pipeline_graph_shared.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/graphics/runtime/runtime.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ShadowPrepareUploadChainBuilder::declare(Core::Alloc::ScratchArena& scratchArena, const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain){
    return declareSelectorAndContextUploads(scratchArena, inputs, chain)
        && declareSurfelFrameConstantsUpload(inputs, chain)
        && declareShadowMaterialContextUploads(inputs, chain)
        && declareSceneBvhUploads(inputs, chain)
    ;
}

Expected<Core::GpuGraphResourceId> ShadowPrepareUploadChainBuilder::importBuffer(const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
    return m_graph.importBuffer(buffer, RendererTaskGraphDetail::BufferResourceDesc(identity, label));
}

bool ShadowPrepareUploadChainBuilder::declareSelectorAndContextUploads(Core::Alloc::ScratchArena& scratchArena, const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain){
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
    if(inputs.rayTracingResources.rayTraceContextReadSubmissionTokenCount != 0u){
        if(!inputs.rayTracingResources.rayTraceContextReadSubmissionTokens)
            return false;
        csgReadCompletions.reserve(inputs.rayTracingResources.rayTraceContextReadSubmissionTokenCount);
        for(usize index = 0u; index < inputs.rayTracingResources.rayTraceContextReadSubmissionTokenCount; ++index){
            const auto& token = inputs.rayTracingResources.rayTraceContextReadSubmissionTokens[index];
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
        .setDependencies(materialContextUploadDependencies, materialContextUploadDependencyCount)
        // Old CSG rays read this persistent selector as well as their retained context buffer.
        .setExternalDependencies(csgReadCompletions.data(), csgReadCompletions.size())
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
            ? Name(RendererFramePipelineDetail::s_HardwareCausticEmissionTargetsName)
            : Name(RendererFramePipelineDetail::s_SoftwareCausticEmissionTargetsName)
    ;
    Expected<Optional<Core::GpuGraphResourceId>> causticEmissionTargetsResult = Optional<Core::GpuGraphResourceId>{};
    if(inputs.rayTracingResources.causticEmissionTargetBuffer){
        const auto importedResource = importBuffer(inputs.rayTracingResources.causticEmissionTargetBuffer, causticEmissionTargetsIdentity, RendererFramePipelineDetail::s_CausticEmissionTargetsLabel);
        if(!importedResource)
            causticEmissionTargetsResult = MakeUnexpected(importedResource.error());
        else
            causticEmissionTargetsResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    if(inputs.rayTracingResources.causticEmissionTargetBuffer && !causticEmissionTargetsResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted caustic emission targets"));
        return false;
    }
    chain.causticEmissionTargets = causticEmissionTargetsResult->value_or(Core::GpuGraphResourceId{});

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
            .setDependencies(&chain.completionTask, 1u)
            .setExternalDependencies(csgReadCompletions.data(), csgReadCompletions.size())
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

        Core::GpuTaskSchedulingHint causticEmissionTargetsUploadScheduling;
        causticEmissionTargetsUploadScheduling.cost = Core::GpuTaskCostHint::Medium;
        causticEmissionTargetsUploadScheduling.forceSubmissionBoundary = false;
        causticEmissionTargetsUploadScheduling.allowPacketMerge = true;
        causticEmissionTargetsUploadScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc causticEmissionTargetsUploadDesc;
        causticEmissionTargetsUploadDesc.setIdentity(Name("render.raytrace.caustic_emission_targets_upload")).setMarkerLabel("Caustic Emission Targets Upload").setScheduling(causticEmissionTargetsUploadScheduling).setDependencies(&chain.completionTask, 1u);
        const auto causticEmissionTargetsUploadTaskResult = m_graph.addUploadBufferTask(
            causticEmissionTargetsUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = **causticEmissionTargetsBlob,
                .destination = chain.causticEmissionTargets,
                // Buffer publishes Common; Shadow Preparation owns the SRV handoff.
                .finalState = Core::ResourceStates::Common, }
        );
        if(!causticEmissionTargetsUploadTaskResult){
            chain.tasks.causticEmissionTargets = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare caustic emission-target upload"));
            return false;
        }
        chain.tasks.causticEmissionTargets = *causticEmissionTargetsUploadTaskResult;
        chain.completionTask = chain.tasks.causticEmissionTargets;
    }
    return true;
}

bool ShadowPrepareUploadChainBuilder::declareSurfelFrameConstantsUpload(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain){
    Expected<Optional<Core::GpuGraphResourceId>> surfelFrameConstantsResult = Optional<Core::GpuGraphResourceId>{};
    if(inputs.rayTracingResources.surfelFrameConstantsBuffer){
        const auto importedResource = importBuffer(inputs.rayTracingResources.surfelFrameConstantsBuffer, Name(RendererFramePipelineDetail::s_SurfelConstantsName), RendererFramePipelineDetail::s_SurfelConstantsLabel);
        if(!importedResource)
            surfelFrameConstantsResult = MakeUnexpected(importedResource.error());
        else
            surfelFrameConstantsResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    if(inputs.rayTracingResources.surfelFrameConstantsBuffer && !surfelFrameConstantsResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted surfel constants"));
        return false;
    }
    chain.surfelFrameConstants = surfelFrameConstantsResult->value_or(Core::GpuGraphResourceId{});

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

        Core::GpuTaskSchedulingHint surfelFrameConstantsUploadScheduling;
        surfelFrameConstantsUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
        surfelFrameConstantsUploadScheduling.forceSubmissionBoundary = false;
        surfelFrameConstantsUploadScheduling.allowPacketMerge = true;
        surfelFrameConstantsUploadScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc surfelFrameConstantsUploadDesc;
        surfelFrameConstantsUploadDesc.setIdentity(Name("render.surfel_gi.constants_upload")).setMarkerLabel("Surfel Frame Constants Upload").setScheduling(surfelFrameConstantsUploadScheduling).setDependencies(&chain.completionTask, 1u);
        const auto surfelFrameConstantsUploadTaskResult = m_graph.addUploadBufferTask(
            surfelFrameConstantsUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = **surfelFrameConstantsBlob,
                .destination = chain.surfelFrameConstants,
                // Buffer publishes Common; Shadow Preparation owns the CB handoff.
                .finalState = Core::ResourceStates::Common, }
        );
        if(!surfelFrameConstantsUploadTaskResult){
            chain.tasks.surfelFrameConstants = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare surfel-frame constants upload"));
            return false;
        }
        chain.tasks.surfelFrameConstants = *surfelFrameConstantsUploadTaskResult;
        chain.completionTask = chain.tasks.surfelFrameConstants;
    }
    return true;
}

bool ShadowPrepareUploadChainBuilder::declareShadowMaterialContextUploads(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain){
    Expected<Optional<Core::GpuGraphResourceId>> shadowInstanceMaterialsResult = Optional<Core::GpuGraphResourceId>{};
    if(inputs.rayTracingResources.shadowInstanceMaterialBuffer){
        const auto importedResource = importBuffer(inputs.rayTracingResources.shadowInstanceMaterialBuffer, Name(RendererFramePipelineDetail::s_SceneShadowInstanceMaterialName), RendererFramePipelineDetail::s_SceneShadowInstanceMaterialLabel);
        if(!importedResource)
            shadowInstanceMaterialsResult = MakeUnexpected(importedResource.error());
        else
            shadowInstanceMaterialsResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    Expected<Optional<Core::GpuGraphResourceId>> shadowInstancesResult = Optional<Core::GpuGraphResourceId>{};
    if(inputs.rayTracingResources.shadowInstanceBuffer){
        const auto importedResource = importBuffer(inputs.rayTracingResources.shadowInstanceBuffer, Name(RendererFramePipelineDetail::s_SceneShadowInstancesName), RendererFramePipelineDetail::s_SceneShadowInstancesLabel);
        if(!importedResource)
            shadowInstancesResult = MakeUnexpected(importedResource.error());
        else
            shadowInstancesResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    Expected<Optional<Core::GpuGraphResourceId>> shadowMaterialTypedResult = Optional<Core::GpuGraphResourceId>{};
    if(inputs.rayTracingResources.shadowMaterialTypedBuffer){
        const auto importedResource = importBuffer(inputs.rayTracingResources.shadowMaterialTypedBuffer, Name(RendererFramePipelineDetail::s_SceneShadowMaterialTypedName), RendererFramePipelineDetail::s_SceneShadowMaterialTypedLabel);
        if(!importedResource)
            shadowMaterialTypedResult = MakeUnexpected(importedResource.error());
        else
            shadowMaterialTypedResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    if((inputs.rayTracingResources.shadowInstanceMaterialBuffer && !shadowInstanceMaterialsResult) || (inputs.rayTracingResources.shadowInstanceBuffer && !shadowInstancesResult) || (inputs.rayTracingResources.shadowMaterialTypedBuffer && !shadowMaterialTypedResult)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted shadow material-context buffers"));
        return false;
    }
    chain.shadowMaterialTyped = shadowMaterialTypedResult->value_or(Core::GpuGraphResourceId{});
    chain.shadowInstances = shadowInstancesResult->value_or(Core::GpuGraphResourceId{});
    chain.shadowInstanceMaterials = shadowInstanceMaterialsResult->value_or(Core::GpuGraphResourceId{});

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

        Core::GpuTaskSchedulingHint shadowMaterialContextUploadScheduling;
        shadowMaterialContextUploadScheduling.cost = Core::GpuTaskCostHint::Medium;
        shadowMaterialContextUploadScheduling.forceSubmissionBoundary = false;
        shadowMaterialContextUploadScheduling.allowPacketMerge = true;
        shadowMaterialContextUploadScheduling.mergeWithPrevious = true;

        Core::GpuTaskDesc shadowInstanceMaterialsUploadDesc;
        shadowInstanceMaterialsUploadDesc
            .setIdentity(Name("render.deferred_effects.instance_material_upload"))
            .setMarkerLabel("Shadow Instance Materials Upload")
            .setScheduling(shadowMaterialContextUploadScheduling)
            .setDependencies(&chain.completionTask, 1u)
        ;
        const auto shadowInstanceMaterialUploadTaskResult = m_graph.addUploadBufferTask(
            shadowInstanceMaterialsUploadDesc,
            Core::GpuUploadBufferTaskDesc{ .source = shadowInstanceMaterialsBlob, .destination = chain.shadowInstanceMaterials, .finalState = Core::ResourceStates::Common, }
        );
        if(!shadowInstanceMaterialUploadTaskResult){
            chain.tasks.shadowInstanceMaterial = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shadow instance-material upload"));
            return false;
        }
        chain.tasks.shadowInstanceMaterial = *shadowInstanceMaterialUploadTaskResult;

        Core::GpuTaskDesc shadowInstancesUploadDesc;
        shadowInstancesUploadDesc.setIdentity(Name("render.deferred_effects.shadow_instances_upload")).setMarkerLabel("Shadow Instances Upload").setScheduling(shadowMaterialContextUploadScheduling).setDependencies(&chain.tasks.shadowInstanceMaterial, 1u);
        const auto shadowInstanceUploadTaskResult = m_graph.addUploadBufferTask(
            shadowInstancesUploadDesc,
            Core::GpuUploadBufferTaskDesc{ .source = shadowInstancesBlob, .destination = chain.shadowInstances, .finalState = Core::ResourceStates::Common, }
        );
        if(!shadowInstanceUploadTaskResult){
            chain.tasks.shadowInstance = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shadow instance upload"));
            return false;
        }
        chain.tasks.shadowInstance = *shadowInstanceUploadTaskResult;

        Core::GpuTaskDesc shadowMaterialTypedUploadDesc;
        shadowMaterialTypedUploadDesc.setIdentity(Name("render.deferred_effects.material_typed_upload")).setMarkerLabel("Shadow Typed Materials Upload").setScheduling(shadowMaterialContextUploadScheduling).setDependencies(&chain.tasks.shadowInstance, 1u);
        const auto shadowMaterialTypedUploadTaskResult = m_graph.addUploadBufferTask(
            shadowMaterialTypedUploadDesc,
            Core::GpuUploadBufferTaskDesc{ .source = shadowMaterialTypedBlob, .destination = chain.shadowMaterialTyped, .finalState = Core::ResourceStates::Common, }
        );
        if(!shadowMaterialTypedUploadTaskResult){
            chain.tasks.shadowMaterialTyped = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shadow typed-material upload"));
            return false;
        }
        chain.tasks.shadowMaterialTyped = *shadowMaterialTypedUploadTaskResult;
        chain.completionTask = chain.tasks.shadowMaterialTyped;
    }
    return true;
}

bool ShadowPrepareUploadChainBuilder::declareSceneBvhUploads(const ShadowPrepareUploadInputs& inputs, ShadowPrepareUploadChain& chain){
    Expected<Optional<Core::GpuGraphResourceId>> sceneBvhNodesResult = Optional<Core::GpuGraphResourceId>{};
    if(inputs.rayTracingResources.sceneBvhNodeBuffer){
        const auto importedResource = importBuffer(inputs.rayTracingResources.sceneBvhNodeBuffer, Name(RendererFramePipelineDetail::s_SceneBvhNodesName), RendererFramePipelineDetail::s_SceneBvhNodesLabel);
        if(!importedResource)
            sceneBvhNodesResult = MakeUnexpected(importedResource.error());
        else
            sceneBvhNodesResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    Expected<Optional<Core::GpuGraphResourceId>> sceneBvhInstancesResult = Optional<Core::GpuGraphResourceId>{};
    if(inputs.rayTracingResources.sceneInstanceBuffer){
        const auto importedResource = importBuffer(inputs.rayTracingResources.sceneInstanceBuffer, Name(RendererFramePipelineDetail::s_SceneInstancesName), RendererFramePipelineDetail::s_SceneInstancesLabel);
        if(!importedResource)
            sceneBvhInstancesResult = MakeUnexpected(importedResource.error());
        else
            sceneBvhInstancesResult = Optional<Core::GpuGraphResourceId>{*importedResource};
    }
    if((inputs.rayTracingResources.sceneBvhNodeBuffer && !sceneBvhNodesResult) || (inputs.rayTracingResources.sceneInstanceBuffer && !sceneBvhInstancesResult)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted software scene-BVH buffers"));
        return false;
    }
    chain.sceneBvhInstances = sceneBvhInstancesResult->value_or(Core::GpuGraphResourceId{});
    chain.sceneBvhNodes = sceneBvhNodesResult->value_or(Core::GpuGraphResourceId{});

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

        Core::GpuTaskSchedulingHint sceneBvhUploadScheduling;
        sceneBvhUploadScheduling.cost = Core::GpuTaskCostHint::Medium;
        sceneBvhUploadScheduling.forceSubmissionBoundary = false;
        sceneBvhUploadScheduling.allowPacketMerge = true;
        sceneBvhUploadScheduling.mergeWithPrevious = true;

        Core::GpuTaskDesc sceneBvhNodesUploadDesc;
        sceneBvhNodesUploadDesc.setIdentity(Name("render.shadow_visibility.scene_bvh_nodes_upload")).setMarkerLabel("Scene BVH Nodes Upload").setScheduling(sceneBvhUploadScheduling).setDependencies(&chain.completionTask, 1u);
        const auto sceneBvhNodesUploadTaskResult = m_graph.addUploadBufferTask(
            sceneBvhNodesUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = sceneBvhNodesBlob,
                .destination = chain.sceneBvhNodes,
                // Scene-BVH storage publishes Common; Shadow Preparation owns the SRV handoff.
                .finalState = Core::ResourceStates::Common, }
        );
        if(!sceneBvhNodesUploadTaskResult){
            chain.tasks.sceneBvhNodes = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare software scene-BVH node upload"));
            return false;
        }
        chain.tasks.sceneBvhNodes = *sceneBvhNodesUploadTaskResult;

        Core::GpuTaskDesc sceneBvhInstancesUploadDesc;
        sceneBvhInstancesUploadDesc.setIdentity(Name("render.shadow_visibility.scene_instances_upload")).setMarkerLabel("Scene BVH Instances Upload").setScheduling(sceneBvhUploadScheduling).setDependencies(&chain.tasks.sceneBvhNodes, 1u);
        const auto sceneBvhInstancesUploadTaskResult = m_graph.addUploadBufferTask(
            sceneBvhInstancesUploadDesc,
            Core::GpuUploadBufferTaskDesc{ .source = sceneBvhInstancesBlob, .destination = chain.sceneBvhInstances, .finalState = Core::ResourceStates::Common, }
        );
        if(!sceneBvhInstancesUploadTaskResult){
            chain.tasks.sceneBvhInstances = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare software scene-BVH instance upload"));
            return false;
        }
        chain.tasks.sceneBvhInstances = *sceneBvhInstancesUploadTaskResult;
        chain.completionTask = chain.tasks.sceneBvhInstances;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

