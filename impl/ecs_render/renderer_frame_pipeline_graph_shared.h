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


inline constexpr char s_SceneShadowInstanceMaterialName[] = "render.deferred_effects.instance_material";
inline constexpr char s_SceneShadowMaterialTypedName[] = "render.deferred_effects.material_typed";
inline constexpr char s_SceneShadowInstancesName[] = "render.deferred_effects.shadow_instances";
inline constexpr char s_SceneShadowInstanceMaterialLabel[] = "Shadow Instance Materials";
inline constexpr char s_SceneShadowMaterialTypedLabel[] = "Shadow Typed Materials";
inline constexpr char s_SceneShadowInstancesLabel[] = "Shadow Instances";
inline constexpr char s_SceneTlasResourceName[] = "render.deferred_effects.tlas";
inline constexpr char s_SceneTlasResourceLabel[] = "Scene TLAS";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Five raytracing scene/shadow buffers shared by the caustics and shadow-visibility graph declares.
// The importer callback matches each file's local importBuffer lambda, so call sites keep their exact graph.
template<typename ImportBufferFn, typename AppendBufferFn>
[[nodiscard]] inline bool AppendRayTracingSceneShadowBuffers(
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
    ImportBufferFn&& importBuffer,
    AppendBufferFn&& appendBuffer
){
    const Core::BufferHandle sceneShadowBuffers[] = {
        rayTracingResources.sceneBvhNodeBuffer,
        rayTracingResources.sceneInstanceBuffer,
        rayTracingResources.shadowInstanceMaterialBuffer,
        rayTracingResources.shadowMaterialTypedBuffer,
        rayTracingResources.shadowInstanceBuffer,
    };
    const Name sceneShadowIdentities[] = {
        Name("render.shadow_visibility.scene_bvh_nodes"),
        Name("render.shadow_visibility.scene_instances"),
        Name(s_SceneShadowInstanceMaterialName),
        Name(s_SceneShadowMaterialTypedName),
        Name(s_SceneShadowInstancesName),
    };
    const char* const sceneShadowLabels[] = {
        "Scene BVH Nodes",
        "Scene Instances",
        s_SceneShadowInstanceMaterialLabel,
        s_SceneShadowMaterialTypedLabel,
        s_SceneShadowInstancesLabel,
    };
    for(usize bufferIndex = 0u; bufferIndex < 5u; ++bufferIndex){
        if(!sceneShadowBuffers[bufferIndex])
            continue;
        const Core::GpuGraphResourceId resource = importBuffer(
            sceneShadowBuffers[bufferIndex],
            sceneShadowIdentities[bufferIndex],
            sceneShadowLabels[bufferIndex]
        );
        if(!resource.valid())
            return false;
        appendBuffer(resource, Core::ResourceStates::ShaderResource);
    }
    return true;
}

// Shared ray-trace set-use assembly for the shadow-visibility and surfel-GI graph declares. Both build the same
// geometry plus material-sampled-texture read uses and differ only in their downstream consumers.
struct TraceResourceSetUses{
    Core::GpuTaskResourceSetUse uses[2u] = {};
    usize useCount = 0u;
};

[[nodiscard]] inline TraceResourceSetUses MakeTraceResourceSetUses(
    const Core::GpuGraphResourceSetId traceGeometrySet,
    const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet
){
    TraceResourceSetUses result;
    if(traceGeometrySet.valid()){
        result.uses[result.useCount++] = Core::GpuTaskResourceSetUse{
            .resourceSet = traceGeometrySet,
            .range = {},
            .requiredState = Core::ResourceStates::ShaderResource,
            .access = Core::GpuTaskResourceAccess::Read,
        };
    }
    if(traceMaterialSampledTextureSet.valid()){
        result.uses[result.useCount++] = Core::GpuTaskResourceSetUse{
            .resourceSet = traceMaterialSampledTextureSet,
            .range = {},
            .requiredState = Core::ResourceStates::ShaderResource,
            .access = Core::GpuTaskResourceAccess::Read,
        };
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

