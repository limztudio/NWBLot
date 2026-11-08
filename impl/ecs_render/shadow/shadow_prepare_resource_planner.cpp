// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_prepare_resource_planner.h"

#include <impl/ecs_render/shadow/shadow_prepare_geometry_resources.h>
#include <impl/ecs_render/shadow/shadow_prepare_upload_chain.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ShadowPrepareResourcePlanner::prepare(const ShadowPrepareResourceInputs& inputs, Core::Alloc::ScratchArena& scratchArena, ShadowPrepareResourcePlan& result){
    using namespace RendererTaskGraphDetail;
    const auto& rayTracingShadowResources = inputs.rayTracingShadowResources;
    const auto& uploads = inputs.uploads;
    const auto currentBindlessSlots = inputs.currentBindlessSlots;
    const auto materialContextSlots = inputs.materialContextSlots;
    const auto& shadowTraceGeometryResources = inputs.shadowTraceGeometryResources;
    const auto& softwareBvhBuildStateResources = inputs.softwareBvhBuildStateResources;
    const auto& preparedMeshBlasBuilds = inputs.preparedMeshBlasBuilds;
    const auto sceneTlasBuildGraphOwned = inputs.sceneTlasBuildGraphOwned;
    const auto meshBlasBuildsGraphOwned = inputs.meshBlasBuildsGraphOwned;
    const auto meshBlasGeometryBuildInputStatesGraphOwned = inputs.meshBlasGeometryBuildInputStatesGraphOwned;
    auto& resourceUses = result.resourceUses;
    auto& resourceSetUses = result.resourceSetUses;
    auto& resourceSetUseCount = result.resourceSetUseCount;
    auto& accelStructFinalizeResourceUses = result.accelStructFinalizeResourceUses;
    auto& sceneTlas = result.sceneTlas;

    const ShadowPrepareGeometryInputs geometryInputs{ .blasBuilds = preparedMeshBlasBuilds, .traceResources = shadowTraceGeometryResources, .blasBuildsGraphOwned = meshBlasBuildsGraphOwned, };
    ShadowPrepareGeometryResources geometryResources(geometryInputs, scratchArena);
    const auto& meshBlasGeometryBuildInputResources = geometryResources.m_blasBuildInputs;
    const auto& shadowPrepareTraceGeometryResources = geometryResources.m_remainingTraceGeometry;
    ECSRenderDetail::MeshBlasGraphStateVector liveMeshBlasGraphStates{ scratchArena };
    m_meshSystem.collectBlasGraphStates(liveMeshBlasGraphStates);
    resourceUses.reserve(s_ShadowPrepareResourceUseCapacity + (uploads.csgTraceContext.valid() ? 1u : 0u) + shadowTraceGeometryResources.size() + softwareBvhBuildStateResources.size() + liveMeshBlasGraphStates.size() + preparedMeshBlasBuilds.size() * 2u);
    accelStructFinalizeResourceUses.reserve((sceneTlasBuildGraphOwned ? 1u : 0u) + preparedMeshBlasBuilds.size());
    geometryResources.prepareStorage(meshBlasGeometryBuildInputStatesGraphOwned);
    // Shadow Preparation owns post-transition boundaries; Compute readers wait on this packet.
    resourceUses.push_back(ReadWriteUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    // Retain Shadow Preparation as producer; WAW handoff retires the immutable upload.
    resourceUses.push_back(WriteUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));
    if(uploads.csgTraceContext.valid())
        resourceUses.push_back(WriteUse(uploads.csgTraceContext, Core::ResourceStates::ShaderResource));
    if(uploads.causticEmissionTargets.valid())
        resourceUses.push_back(WriteUse(uploads.causticEmissionTargets, Core::ResourceStates::ShaderResource));
    if(uploads.surfelFrameConstants.valid())
        resourceUses.push_back(WriteUse(uploads.surfelFrameConstants, Core::ResourceStates::ConstantBuffer));
    if(uploads.shadowInstanceMaterials.valid())
        resourceUses.push_back(WriteUse(uploads.shadowInstanceMaterials, Core::ResourceStates::ShaderResource));
    if(uploads.shadowInstances.valid())
        resourceUses.push_back(WriteUse(uploads.shadowInstances, Core::ResourceStates::ShaderResource));
    if(uploads.shadowMaterialTyped.valid())
        resourceUses.push_back(WriteUse(uploads.shadowMaterialTyped, Core::ResourceStates::ShaderResource));
    if(uploads.sceneBvhNodes.valid())
        resourceUses.push_back(WriteUse(uploads.sceneBvhNodes, Core::ResourceStates::ShaderResource));
    if(uploads.sceneBvhInstances.valid())
        resourceUses.push_back(WriteUse(uploads.sceneBvhInstances, Core::ResourceStates::ShaderResource));

    b8 resourcesImported = true;
    if(!geometryResources.gatherBuildInputs(m_graph)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: frozen BLAS geometry inputs are missing graph resources"));
        return false;
    }
    if(meshBlasBuildsGraphOwned){
        for(const PreparedMeshBlasBuild& build : preparedMeshBlasBuilds){
            const auto blasIdentity = DeriveName(build.meshName, s_BlasNameSuffix);
            if(!blasIdentity){
                resourcesImported = false;
                continue;
            }
            // First build knows native Common; retained storage imports the accepted binding.
            const Core::ResourceStates::Mask blasInitialState = build.backingFresh ? Core::ResourceStates::Common : Core::ResourceStates::Unknown;
            const auto blas = m_graph.importAccelStruct(build.blas, AccelStructResourceDesc(*blasIdentity, "Prepared Mesh BLAS").setInitialState(blasInitialState));
            resourcesImported = resourcesImported && blas.has_value();
            if(blas.has_value()){
                // Typed acceleration-structure uses lower through their backing allocation.
                resourceUses.push_back(ReadWriteUse(*blas, Core::ResourceStates::AccelStructWrite));
                accelStructFinalizeResourceUses.push_back(ReadUse(*blas, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    Core::GpuGraphResourceSetId meshBlasGeometryBuildInputSet;
    if(meshBlasGeometryBuildInputStatesGraphOwned && !meshBlasGeometryBuildInputResources.empty()){
        const auto meshBlasGeometryBuildInputSetResult = m_graph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name(s_ShadowPrepareBlasGeometryBuildInputs))
                .setMarkerLabel("Shadow Prepare BLAS Geometry Build Inputs")
                .setMembers(meshBlasGeometryBuildInputResources)
        );
        if(meshBlasGeometryBuildInputSetResult)
            meshBlasGeometryBuildInputSet = *meshBlasGeometryBuildInputSetResult;
    }
    const b8 meshBlasGeometryBuildInputSetGraphOwned = meshBlasGeometryBuildInputSet.valid();
    if(meshBlasGeometryBuildInputStatesGraphOwned && !meshBlasGeometryBuildInputSetGraphOwned){
        for(const Core::GpuGraphResourceId resource : meshBlasGeometryBuildInputResources)
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::AccelStructBuildInput));
    }
    const Core::GpuTaskResourceSetUse meshBlasGeometryBuildInputSetUse{ .resourceSet = meshBlasGeometryBuildInputSet, .range = {}, .requiredState = Core::ResourceStates::AccelStructBuildInput, .access = Core::GpuTaskResourceAccess::ReadWrite, };
    if(meshBlasGeometryBuildInputSetGraphOwned)
        resourceSetUses[resourceSetUseCount++] = meshBlasGeometryBuildInputSetUse;
    if(!geometryResources.gatherRemainingTraceResources())
        return false;


    // Frozen trace list may carry build inputs; keep them separate from the SRV subset.
    Core::GpuGraphResourceSetId shadowPrepareTraceGeometrySet;
    if(!shadowPrepareTraceGeometryResources.empty()){
        const auto shadowPrepareTraceGeometrySetResult = m_graph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name(s_ShadowPrepareTraceGeometry))
                .setMarkerLabel("Shadow Prepare Trace Geometry")
                .setMembers(shadowPrepareTraceGeometryResources)
        );
        if(shadowPrepareTraceGeometrySetResult)
            shadowPrepareTraceGeometrySet = *shadowPrepareTraceGeometrySetResult;
    }
    const b8 shadowPrepareTraceGeometryStatesGraphOwned = shadowPrepareTraceGeometrySet.valid();
    if(!shadowPrepareTraceGeometryStatesGraphOwned){
        for(const Core::GpuGraphResourceId resource : shadowPrepareTraceGeometryResources)
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::ShaderResource));
    }
    const Core::GpuTaskResourceSetUse shadowPrepareTraceGeometrySetUse{ .resourceSet = shadowPrepareTraceGeometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::ReadWrite, };
    if(shadowPrepareTraceGeometryStatesGraphOwned)
        resourceSetUses[resourceSetUseCount++] = shadowPrepareTraceGeometrySetUse;
    for(const Core::GpuGraphResourceId resource : softwareBvhBuildStateResources){
        if(!resource.valid())
            return false;
    }
    Core::GpuGraphResourceSetId softwareBvhBuildStateSet;
    if(!softwareBvhBuildStateResources.empty()){
        const auto softwareBvhBuildStateSetResult = m_graph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(Name(s_ShadowPrepareSoftwareBvhBuildState))
                .setMarkerLabel("Shadow Prepare Software BVH Build State")
                .setMembers(softwareBvhBuildStateResources)
        );
        if(softwareBvhBuildStateSetResult)
            softwareBvhBuildStateSet = *softwareBvhBuildStateSetResult;
    }
    const b8 softwareBvhBuildStateStatesGraphOwned = softwareBvhBuildStateSet.valid();
    if(!softwareBvhBuildStateStatesGraphOwned){
        for(const Core::GpuGraphResourceId resource : softwareBvhBuildStateResources){
            // Parent links and scratch keep native UAV state; they are state-only resources.
            resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::UnorderedAccess));
        }
    }
    const Core::GpuTaskResourceSetUse softwareBvhBuildStateSetUse{ .resourceSet = softwareBvhBuildStateSet, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::ReadWrite, };
    if(softwareBvhBuildStateStatesGraphOwned)
        resourceSetUses[resourceSetUseCount++] = softwareBvhBuildStateSetUse;

    if(rayTracingShadowResources.sceneTlas){
        // Imports observe the backing generation; retained storage stays Unknown until accepted.
        const Core::ResourceStates::Mask sceneTlasInitialState = m_raytracingSystem.sceneTlasBackingInitialState();
        const auto sceneTlasResult = m_graph.importAccelStruct(
            rayTracingShadowResources.sceneTlas,
            AccelStructResourceDesc(Name(s_SceneTlasResourceName), s_SceneTlasResourceLabel).setInitialState(sceneTlasInitialState)
        );
        if(sceneTlasResult)
            sceneTlas = *sceneTlasResult;
        resourcesImported = resourcesImported && sceneTlasResult.has_value();
        if(sceneTlas.valid()){
            if(sceneTlasBuildGraphOwned){
                // Frozen recorder only builds; graph lowers Write entry, successor lowers Read handoff.
                resourceUses.push_back(ReadWriteUse(sceneTlas, Core::ResourceStates::AccelStructWrite));
                accelStructFinalizeResourceUses.push_back(ReadUse(sceneTlas, Core::ResourceStates::AccelStructRead));
            }
            else{
                // Cached scene TLAS remains read-ready; retain its accepted-state handoff.
                resourceUses.push_back(ReadWriteUse(sceneTlas, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    for(const ECSRenderDetail::MeshBlasGraphState& state : liveMeshBlasGraphStates){
        // Frozen plans own retained handles; never collide replacements with frozen identities.
        if(geometryResources.isPreparedMeshBlasBuild(state.meshName))
            continue;

        const auto blasIdentity = DeriveName(state.meshName, s_BlasNameSuffix);
        if(!blasIdentity){
            resourcesImported = false;
            continue;
        }
        const Core::ResourceStates::Mask blasInitialState = state.backingFresh ? Core::ResourceStates::Common : Core::ResourceStates::Unknown;
        const auto blas = m_graph.importAccelStruct(state.blas, AccelStructResourceDesc(*blasIdentity, "Mesh BLAS").setInitialState(blasInitialState));
        resourcesImported = resourcesImported && blas.has_value();
        if(blas.has_value()){
            if(state.nativeBuildsBlas){
                // Retained backing keeps its accepted read state until the selected build is published.
                resourceUses.push_back(ReadWriteUse(*blas, Core::ResourceStates::AccelStructRead));
            }
            else{
                // State-only import; rejected preparation re-pends and seeds accepted state.
                resourceUses.push_back(ReadUse(*blas, Core::ResourceStates::AccelStructRead));
            }
        }
    }
    if(!resourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import preflighted shadow-preparation resources"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

