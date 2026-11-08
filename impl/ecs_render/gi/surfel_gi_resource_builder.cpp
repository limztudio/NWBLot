// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "surfel_gi_resource_builder.h"

#include <impl/ecs_render/renderer_frame_pipeline.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SurfelGiResourceBuilder::prepare(const SurfelGiResourceInputs& inputs, SurfelGiResourcePlan& result){
    using namespace RendererTaskGraphDetail;
    const auto& deferredTargets = inputs.deferredTargets;
    const auto& rayTracingResources = inputs.rayTracingResources;
    const auto& rayTracingSurfelResources = inputs.rayTracingSurfelResources;
    const auto worldPosition = inputs.worldPosition;
    const auto normal = inputs.normal;
    const auto surfelIrradiance = inputs.surfelIrradiance;
    const auto currentBindlessSlots = inputs.currentBindlessSlots;
    const auto sceneShading = inputs.sceneShading;
    const auto lights = inputs.lights;
    const auto materialContextSlots = inputs.materialContextSlots;
    const auto& traceGeometryResources = inputs.traceGeometryResources;
    const auto traceGeometryStatesGraphOwned = inputs.traceGeometryStatesGraphOwned;
    const auto useHwTrace = inputs.useHwTrace;
    const auto hasSurfelWork = inputs.hasSurfelWork;
    auto& surfelIrradianceHalf = result.surfelIrradianceHalf;
    auto& sceneGeometryDomain = result.sceneGeometryDomain;
    auto& shadowInstanceMaterials = result.shadowInstanceMaterials;
    auto& shadowMaterialTyped = result.shadowMaterialTyped;
    auto& shadowInstances = result.shadowInstances;
    auto& csgTraceContext = result.csgTraceContext;
    auto& surfelConstants = result.surfelConstants;
    auto& surfelPool = result.surfelPool;
    auto& surfelGuidePool = result.surfelGuidePool;
    auto& surfelCellHead = result.surfelCellHead;
    auto& surfelCounter = result.surfelCounter;
    auto& surfelTraceIndirectArgs = result.surfelTraceIndirectArgs;
    auto& surfelFreeList = result.surfelFreeList;
    auto& surfelPoolSnapshot = result.surfelPoolSnapshot;
    auto& surfelCellHeadSnapshot = result.surfelCellHeadSnapshot;
    auto& sceneBvhNodes = result.sceneBvhNodes;
    auto& sceneInstances = result.sceneInstances;
    auto& tlas = result.tlas;
    auto& resourceUses = result.resourceUses;
    auto& traceResourceUses = result.traceResourceUses;
    auto& graphOwnsSurfelGiResolve = result.graphOwnsSurfelGiResolve;

    const auto surfelIrradianceHalfResult = ImportExternalTexture(m_graph, deferredTargets.surfelIrradianceHalf, Name(s_SurfelGraphIrradianceHalf), "Surfel Irradiance Half");
    const auto sceneGeometryDomainResult = m_graph.importHazardDomain(HazardDomainDesc(Name(s_SurfelGraphSceneGeometry), s_SceneGeometryDomainLabel));
    if(!surfelIrradianceHalfResult || !sceneGeometryDomainResult){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred surfel-GI graph resources"));
        return false;
    }
    surfelIrradianceHalf = *surfelIrradianceHalfResult;
    sceneGeometryDomain = *sceneGeometryDomainResult;

    const b8 hasCsgTraceContext = rayTracingResources.csgTraceContextBuffer.get() != nullptr;
    resourceUses.reserve(s_SurfelResourceUseCapacity + (hasCsgTraceContext ? 1u : 0u) + (traceGeometryStatesGraphOwned ? 0u : traceGeometryResources.size()));
    traceResourceUses.reserve(s_SurfelTraceResourceUseCapacity + (hasCsgTraceContext ? s_SurfelCsgTraceContextUseCapacity : 0u) + rayTracingResources.csgTraceBoundsBuffers.size() + (traceGeometryStatesGraphOwned ? 0u : traceGeometryResources.size()));
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(normal));
    resourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(WriteUse(surfelIrradianceHalf, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(WriteUse(surfelIrradiance, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
    resourceUses.push_back(ReadUse(sceneGeometryDomain));
    if(materialContextSlots.valid())
        resourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    for(const Core::GpuGraphResourceId resource : traceGeometryResources){
        if(!resource.valid())
            return false;
        if(!traceGeometryStatesGraphOwned)
            resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
    }

    const auto appendOptionalBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state, const b8 writable = false) -> Expected<Core::GpuGraphResourceId>{
        if(!buffer)
            return Core::GpuGraphResourceId{};
        const auto resourceResult = ImportExternalBuffer(m_graph, buffer, identity, label);
        if(!resourceResult)
            return MakeUnexpected(Failure{});
        const Core::GpuGraphResourceId resource = *resourceResult;
        resourceUses.push_back(writable ? WriteUse(resource, state) : ReadUse(resource, state));
        return resource;
    };
    const auto importOptionalResources = [&]() -> Expected<void>{
        const auto shadowInstanceMaterialsResult = appendOptionalBuffer(
            rayTracingResources.shadowInstanceMaterialBuffer,
            Name(s_SceneShadowInstanceMaterialName),
            s_SceneShadowInstanceMaterialLabel,
            Core::ResourceStates::ShaderResource
        );
        if(!shadowInstanceMaterialsResult)
            return MakeUnexpected(Failure{});
        shadowInstanceMaterials = *shadowInstanceMaterialsResult;
        const auto shadowMaterialTypedResult = appendOptionalBuffer(
            rayTracingResources.shadowMaterialTypedBuffer,
            Name(s_SceneShadowMaterialTypedName),
            s_SceneShadowMaterialTypedLabel,
            Core::ResourceStates::ShaderResource
        );
        if(!shadowMaterialTypedResult)
            return MakeUnexpected(Failure{});
        shadowMaterialTyped = *shadowMaterialTypedResult;
        const auto shadowInstancesResult = appendOptionalBuffer(
            rayTracingResources.shadowInstanceBuffer,
            Name(s_SceneShadowInstancesName),
            s_SceneShadowInstancesLabel,
            Core::ResourceStates::ShaderResource
        );
        if(!shadowInstancesResult)
            return MakeUnexpected(Failure{});
        shadowInstances = *shadowInstancesResult;
        const auto surfelConstantsResult = appendOptionalBuffer(rayTracingSurfelResources.constantsBuffer, Name(s_SurfelConstantsName), s_SurfelConstantsLabel, Core::ResourceStates::ConstantBuffer);
        if(!surfelConstantsResult)
            return MakeUnexpected(Failure{});
        surfelConstants = *surfelConstantsResult;
        const auto surfelPoolResult = appendOptionalBuffer(rayTracingSurfelResources.poolBuffer, Name(s_SurfelGraphPool), "Surfel Pool", Core::ResourceStates::UnorderedAccess, true);
        if(!surfelPoolResult)
            return MakeUnexpected(Failure{});
        surfelPool = *surfelPoolResult;
        const auto surfelGuidePoolResult = appendOptionalBuffer(rayTracingSurfelResources.guidePoolBuffer, Name(s_SurfelGraphGuidePool), "Surfel Guide Pool", Core::ResourceStates::UnorderedAccess, true);
        if(!surfelGuidePoolResult)
            return MakeUnexpected(Failure{});
        surfelGuidePool = *surfelGuidePoolResult;
        const auto surfelCellHeadResult = appendOptionalBuffer(rayTracingSurfelResources.cellHeadBuffer, Name(s_SurfelGraphCellHeads), "Surfel Cell Heads", Core::ResourceStates::UnorderedAccess, true);
        if(!surfelCellHeadResult)
            return MakeUnexpected(Failure{});
        surfelCellHead = *surfelCellHeadResult;
        const auto surfelCounterResult = appendOptionalBuffer(rayTracingSurfelResources.counterBuffer, Name(s_SurfelCounterName), s_SurfelCounterLabel, Core::ResourceStates::UnorderedAccess, true);
        if(!surfelCounterResult)
            return MakeUnexpected(Failure{});
        surfelCounter = *surfelCounterResult;
        const auto surfelFreeListResult = appendOptionalBuffer(rayTracingSurfelResources.freeListBuffer, Name(s_SurfelGraphFreeList), "Surfel Free List", Core::ResourceStates::UnorderedAccess, true);
        if(!surfelFreeListResult)
            return MakeUnexpected(Failure{});
        surfelFreeList = *surfelFreeListResult;
        const auto surfelPoolSnapshotResult = appendOptionalBuffer(rayTracingSurfelResources.poolSnapshotBuffer, Name(s_SurfelGraphPoolSnapshot), "Surfel Pool Snapshot", Core::ResourceStates::ShaderResource);
        if(!surfelPoolSnapshotResult)
            return MakeUnexpected(Failure{});
        surfelPoolSnapshot = *surfelPoolSnapshotResult;
        const auto surfelCellHeadSnapshotResult = appendOptionalBuffer(rayTracingSurfelResources.cellHeadSnapshotBuffer, Name(s_SurfelGraphCellHeadSnapshot), "Surfel Cell Head Snapshot", Core::ResourceStates::ShaderResource);
        if(!surfelCellHeadSnapshotResult)
            return MakeUnexpected(Failure{});
        surfelCellHeadSnapshot = *surfelCellHeadSnapshotResult;
        return {};
    };
    b8 optionalResourcesImported = importOptionalResources().has_value();
    if(optionalResourcesImported && rayTracingResources.csgTraceContextBuffer){
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(m_graph);
            csgTraceContext = declarations.findImportedBuffer(rayTracingResources.csgTraceContextBuffer);
        }
        optionalResourcesImported = csgTraceContext.valid();
        if(optionalResourcesImported)
            resourceUses.push_back(ReadUse(csgTraceContext, Core::ResourceStates::ShaderResource));
    }
    if(optionalResourcesImported && rayTracingSurfelResources.traceIndirectArgsBuffer){
        const auto surfelTraceIndirectArgsResult = ImportExternalBuffer(m_graph, rayTracingSurfelResources.traceIndirectArgsBuffer, Name(s_SurfelGraphTraceArgs), "Surfel Trace Arguments");
        if(surfelTraceIndirectArgsResult)
            surfelTraceIndirectArgs = *surfelTraceIndirectArgsResult;
        optionalResourcesImported = surfelTraceIndirectArgsResult.has_value();
    }
    if(optionalResourcesImported && !useHwTrace){
        const auto sceneBvhNodesResult = appendOptionalBuffer(rayTracingResources.sceneBvhNodeBuffer, Name(s_SceneBvhNodesName), s_SceneBvhNodesLabel, Core::ResourceStates::ShaderResource);
        if(sceneBvhNodesResult)
            sceneBvhNodes = *sceneBvhNodesResult;
        optionalResourcesImported = sceneBvhNodesResult.has_value();
    }
    if(optionalResourcesImported && !useHwTrace){
        const auto sceneInstancesResult = appendOptionalBuffer(rayTracingResources.sceneInstanceBuffer, Name(s_SceneInstancesName), s_SceneInstancesLabel, Core::ResourceStates::ShaderResource);
        if(sceneInstancesResult)
            sceneInstances = *sceneInstancesResult;
        optionalResourcesImported = sceneInstancesResult.has_value();
    }
    if(optionalResourcesImported && useHwTrace){
        if(!rayTracingResources.sceneTlas)
            optionalResourcesImported = false;
        else{
            const auto tlasResult = m_graph.importAccelStruct(
                rayTracingResources.sceneTlas,
                AccelStructResourceDesc(Name(s_SceneTlasResourceName), s_SceneTlasResourceLabel)
                    .setInitialState(m_raytracingSystem.sceneTlasBackingInitialState())
            );
            if(tlasResult)
                tlas = *tlasResult;
            optionalResourcesImported = tlasResult.has_value();
            if(optionalResourcesImported)
                resourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
        }
    }
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a deferred surfel-GI dynamic resource domain"));
        return false;
    }

    graphOwnsSurfelGiResolve =
        hasSurfelWork
        && shadowInstanceMaterials.valid()
        && shadowMaterialTyped.valid()
        && shadowInstances.valid()
        && materialContextSlots.valid()
        && surfelConstants.valid()
        && surfelPool.valid()
        && surfelGuidePool.valid()
        && surfelCellHead.valid()
        && surfelCounter.valid()
        && surfelTraceIndirectArgs.valid()
        && surfelFreeList.valid()
        && surfelPoolSnapshot.valid()
        && surfelCellHeadSnapshot.valid()
        && (useHwTrace ? tlas.valid() : (sceneBvhNodes.valid() && sceneInstances.valid()))
        && rayTracingResources.surfelSplitGraphPipelinesReady
    ;
    if(hasSurfelWork && !graphOwnsSurfelGiResolve){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: active surfel GI has no complete prepared graph resources"));
        return false;
    }
    if(graphOwnsSurfelGiResolve){
        resourceUses.clear();
        resourceUses.push_back(ReadUse(worldPosition));
        resourceUses.push_back(ReadUse(normal));
        resourceUses.push_back(ReadUse(surfelIrradianceHalf, Core::ResourceStates::ShaderResource));
        resourceUses.push_back(WriteUse(surfelIrradiance, Core::ResourceStates::UnorderedAccess));

        traceResourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        traceResourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
        traceResourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(sceneGeometryDomain));
        traceResourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
        traceResourceUses.push_back(ReadUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(shadowMaterialTyped, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(shadowInstances, Core::ResourceStates::ShaderResource));
        if(csgTraceContext.valid()){
            if(!RendererFramePipelineDetail::AppendCurrentCsgRayGeometry(
                rayTracingResources.csgTraceContextBuffer,
                rayTracingResources.csgTraceBoundsBuffers,
                m_graph,
                traceResourceUses
            ))
                return false;
            traceResourceUses.push_back(ReadWriteUse(surfelCounter, Core::ResourceStates::UnorderedAccess));
        }
        traceResourceUses.push_back(ReadUse(surfelConstants, Core::ResourceStates::ConstantBuffer));
        traceResourceUses.push_back(ReadWriteUse(surfelPool, Core::ResourceStates::UnorderedAccess));
        traceResourceUses.push_back(ReadWriteUse(surfelGuidePool, Core::ResourceStates::UnorderedAccess));
        traceResourceUses.push_back(ReadUse(surfelPoolSnapshot, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(surfelCellHeadSnapshot, Core::ResourceStates::ShaderResource));
        traceResourceUses.push_back(ReadUse(surfelTraceIndirectArgs, Core::ResourceStates::IndirectArgument));
        if(!traceGeometryStatesGraphOwned){
            for(const Core::GpuGraphResourceId resource : traceGeometryResources)
                traceResourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));
        }
        if(useHwTrace)
            traceResourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
        else{
            traceResourceUses.push_back(ReadUse(sceneBvhNodes, Core::ResourceStates::ShaderResource));
            traceResourceUses.push_back(ReadUse(sceneInstances, Core::ResourceStates::ShaderResource));
        }
    }
    else if(surfelTraceIndirectArgs.valid())
        resourceUses.push_back(WriteUse(surfelTraceIndirectArgs, Core::ResourceStates::UnorderedAccess));

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

