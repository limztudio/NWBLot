// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererFramePipelineDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_SceneShadowInstanceMaterialName = "render.deferred_effects.instance_material";
inline constexpr AStringView s_SceneShadowMaterialTypedName = "render.deferred_effects.material_typed";
inline constexpr AStringView s_SceneShadowInstancesName = "render.deferred_effects.shadow_instances";
inline constexpr AStringView s_SceneShadowInstanceMaterialLabel = "Shadow Instance Materials";
inline constexpr AStringView s_SceneShadowMaterialTypedLabel = "Shadow Typed Materials";
inline constexpr AStringView s_SceneShadowInstancesLabel = "Shadow Instances";
inline constexpr AStringView s_SceneTlasResourceName = "render.deferred_effects.tlas";
inline constexpr AStringView s_SceneTlasResourceLabel = "Scene TLAS";
inline constexpr AStringView s_SceneBvhNodesName = "render.shadow_visibility.scene_bvh_nodes";
inline constexpr AStringView s_SceneBvhNodesLabel = "Scene BVH Nodes";
inline constexpr AStringView s_SceneInstancesName = "render.shadow_visibility.scene_instances";
inline constexpr AStringView s_SceneInstancesLabel = "Scene Instances";
inline constexpr AStringView s_SceneGeometryDomainLabel = "Scene Acceleration and Geometry";
// Shadow-visibility targets shared by the monolithic declaration and the prepared split re-imports.
inline constexpr AStringView s_ShadowSoftHalfAName = "render.shadow_visibility.soft_half_a";
inline constexpr AStringView s_ShadowSoftHalfALabel = "Shadow Soft Half A";
inline constexpr AStringView s_ShadowSoftHalfBName = "render.shadow_visibility.soft_half_b";
inline constexpr AStringView s_ShadowSoftHalfBLabel = "Shadow Soft Half B";
inline constexpr AStringView s_ShadowSoftGeometryName = "render.shadow_visibility.soft_geometry";
inline constexpr AStringView s_ShadowSoftGeometryLabel = "Shadow Soft Geometry";
inline constexpr AStringView s_ShadowSoftGeometryPreviousName = "render.shadow_visibility.soft_geometry_previous";
inline constexpr AStringView s_ShadowSoftGeometryPreviousLabel = "Previous Shadow Soft Geometry";
inline constexpr AStringView s_ShadowHistoryAName = "render.shadow_visibility.history_a";
inline constexpr AStringView s_ShadowHistoryALabel = "Shadow History A";
inline constexpr AStringView s_ShadowHistoryBName = "render.shadow_visibility.history_b";
inline constexpr AStringView s_ShadowHistoryBLabel = "Shadow History B";
inline constexpr AStringView s_ShadowMomentsAName = "render.shadow_visibility.moments_a";
inline constexpr AStringView s_ShadowMomentsALabel = "Shadow Moments A";
inline constexpr AStringView s_ShadowMomentsBName = "render.shadow_visibility.moments_b";
inline constexpr AStringView s_ShadowMomentsBLabel = "Shadow Moments B";
inline constexpr AStringView s_TransparentSoftHalfName = "render.shadow_visibility.transparent_soft_half";
inline constexpr AStringView s_TransparentSoftHalfLabel = "Transparent Shadow Soft Half";
inline constexpr AStringView s_TransparentHistoryAName = "render.shadow_visibility.transparent_history_a";
inline constexpr AStringView s_TransparentHistoryALabel = "Transparent Shadow History A";
inline constexpr AStringView s_TransparentHistoryBName = "render.shadow_visibility.transparent_history_b";
inline constexpr AStringView s_TransparentHistoryBLabel = "Transparent Shadow History B";
inline constexpr AStringView s_TransparentMomentsAName = "render.shadow_visibility.transparent_moments_a";
inline constexpr AStringView s_TransparentMomentsALabel = "Transparent Shadow Moments A";
inline constexpr AStringView s_TransparentMomentsBName = "render.shadow_visibility.transparent_moments_b";
inline constexpr AStringView s_TransparentMomentsBLabel = "Transparent Shadow Moments B";
inline constexpr AStringView s_ShadowEdgeCounterName = "render.shadow_visibility.edge_counter";
inline constexpr AStringView s_ShadowEdgeCounterLabel = "Shadow Edge Counter";
inline constexpr AStringView s_MeshViewName = "render.deferred.mesh_view";
inline constexpr AStringView s_MeshViewLabel = "Mesh View";
inline constexpr AStringView s_CausticAccumulatorLabel = "Caustic Accumulator";
inline constexpr AStringView s_CausticHistoryLabel = "Caustic History";
inline constexpr AStringView s_CausticResolveHalfLabel = "Caustic Resolve Half";
inline constexpr AStringView s_CausticResolveGeometryLabel = "Caustic Resolve Geometry";
inline constexpr AStringView s_HardwareCausticEmissionTargetsName = "render.hardware_caustics.emission_targets";
inline constexpr AStringView s_SoftwareCausticEmissionTargetsName = "render.software_caustics.emission_targets";
inline constexpr AStringView s_CausticEmissionTargetsLabel = "Caustic Emission Targets";
inline constexpr AStringView s_SurfelConstantsName = "render.surfel_gi.constants";
inline constexpr AStringView s_SurfelConstantsLabel = "Surfel Constants";
inline constexpr AStringView s_SurfelCounterName = "render.surfel_gi.counter";
inline constexpr AStringView s_SurfelCounterLabel = "Surfel Counter";
inline constexpr AStringView s_SurfelCounterReadbackName = "render.surfel_gi.counter_readback";
inline constexpr AStringView s_SurfelCounterReadbackLabel = "Surfel Counter Readback";
inline constexpr AStringView s_BlasNameSuffix = ":blas";
inline constexpr AStringView s_SoftwareSceneRefitInputsName = "software_scene_refit_inputs";

// Caustic resolve geometry, prepare, and wavelet stages declare three resource uses each; upsample declares five.
inline constexpr usize s_CausticResolveStageResourceUseCapacity = 3u;
inline constexpr usize s_CausticResolveUpsampleResourceUseCapacity = 5u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Five raytracing scene/shadow buffers shared by the caustics and shadow-visibility graph declares.
// The importer callback matches each file's local importBuffer lambda, so call sites keep their exact graph.
template<typename ImportBufferFn, typename AppendBufferFn>
[[nodiscard]] inline bool AppendRayTracingSceneShadowBuffers(const RayTracingDeferredGraphResourceSnapshot& rayTracingResources, ImportBufferFn&& importBuffer, AppendBufferFn&& appendBuffer){
    const ReferenceWrapper<const Core::BufferHandle> sceneShadowBuffers[] = {
        rayTracingResources.sceneBvhNodeBuffer,
        rayTracingResources.sceneInstanceBuffer,
        rayTracingResources.shadowInstanceMaterialBuffer,
        rayTracingResources.shadowMaterialTypedBuffer,
        rayTracingResources.shadowInstanceBuffer,
    };
    const Name sceneShadowIdentities[] = { Name(s_SceneBvhNodesName), Name(s_SceneInstancesName), Name(s_SceneShadowInstanceMaterialName), Name(s_SceneShadowMaterialTypedName), Name(s_SceneShadowInstancesName), };
    const AStringView sceneShadowLabels[] = { s_SceneBvhNodesLabel, s_SceneInstancesLabel, s_SceneShadowInstanceMaterialLabel, s_SceneShadowMaterialTypedLabel, s_SceneShadowInstancesLabel, };
    for(usize bufferIndex = 0u; bufferIndex < LengthOf(sceneShadowBuffers); ++bufferIndex){
        const Core::BufferHandle& buffer = sceneShadowBuffers[bufferIndex];
        if(!buffer)
            continue;
        const auto resourceResult = importBuffer(buffer, sceneShadowIdentities[bufferIndex], sceneShadowLabels[bufferIndex]);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        appendBuffer(resource, Core::ResourceStates::ShaderResource);
    }
    return true;
}

struct TraceResourceSetUses{
    // Trace geometry set and trace material sampled-texture set.
    static constexpr usize s_UseCapacity = 2u;

    Core::GpuTaskResourceSetUse uses[s_UseCapacity] = {};
    usize useCount = 0u;
};

template<typename AppendBufferFn>
[[nodiscard]] inline bool AppendCurrentCsgRayGeometry(Core::GpuTaskGraph& graph, const Core::BufferHandle& context, const Core::BufferHandle* bounds, const usize boundsCount, AppendBufferFn&& appendBuffer){
    if(!context)
        return true;
    if(boundsCount != 0u && !bounds)
        return false;
    const auto appendRead = [&](const Core::BufferHandle& buffer){
        if(!buffer)
            return false;
        Core::GpuGraphResourceId resource;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            if(!declarations.valid())
                return false;
            resource = declarations.findImportedBuffer(buffer);
        }
        if(!resource.valid()){
            const auto importedResource = graph.importBuffer(buffer, RendererTaskGraphDetail::BufferResourceDesc(buffer->getCreationDescription().debugName, "Current CSG Ray Geometry"));
            if(!importedResource)
                return false;
            resource = *importedResource;
        }
        if(!resource.valid())
            return false;
        appendBuffer(resource);
        return true;
    };
    if(!appendRead(context))
        return false;
    for(usize index = 0u; index < boundsCount; ++index){
        if(!appendRead(bounds[index]))
            return false;
    }
    return true;
}

[[nodiscard]] inline TraceResourceSetUses MakeTraceResourceSetUses(const Core::GpuGraphResourceSetId traceGeometrySet, const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet)noexcept{
    TraceResourceSetUses result;
    if(traceGeometrySet.valid()){
        result.uses[result.useCount++] = Core::GpuTaskResourceSetUse{ .resourceSet = traceGeometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    }
    if(traceMaterialSampledTextureSet.valid()){
        result.uses[result.useCount++] = Core::GpuTaskResourceSetUse{ .resourceSet = traceMaterialSampledTextureSet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

