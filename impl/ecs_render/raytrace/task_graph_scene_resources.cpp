// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_scene_resources.h"

#include <impl/ecs_render/optics/task_graph_optical_scene_upload.h>

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RayTracingSceneGraphResources::valid()const noexcept{
    return hardwareAvailable && sceneTlas && tlasHeapHandle.valid() && materialContextSlotsBuffer && instanceMaterialBuffer && materialTypedBuffer && instanceBuffer && opticalScene.valid();
}

b8 RayTracingSceneGraphReads::valid()const noexcept{
    for(const auto& use : uses){
        if(!use.resource.valid())
            return false;
    }
    for(const auto& use : csgUses){
        if(!use.resource.valid())
            return false;
    }
    return true;
}

Expected<RayTracingSceneGraphReads> ImportRayTracingSceneGraphReads(
    const RayTracingSceneGraphResources& resources,
    const Core::ResourceStates::Mask tlasInitialState,
    const Core::GpuTaskId priorReadersDrained,
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena
){
    if(!resources.valid())
        return MakeUnexpected(Failure{});

    const Array<ReferenceWrapper<const Core::BufferHandle>, s_SceneImportedBufferCount> buffers = {
        resources.materialContextSlotsBuffer,
        resources.instanceMaterialBuffer,
        resources.materialTypedBuffer,
        resources.instanceBuffer,
    };
    Array<Core::GpuGraphResourceId, s_SceneImportedBufferCount> importedBuffers{};
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return MakeUnexpected(Failure{});
        for(usize index = 0u; index < s_SceneImportedBufferCount; ++index)
            importedBuffers[index] = declarations.findImportedBuffer(buffers[index]);
    }
    for(usize index = 0u; index < s_SceneImportedBufferCount; ++index){
        if(importedBuffers[index].valid())
            continue;
        const Core::BufferHandle& buffer = buffers[index];
        const Name identity = buffer->getCreationDescription().debugName;
        if(!identity)
            return MakeUnexpected(Failure{});
        const auto importedBufferResult = graph.importBuffer(buffer, RendererTaskGraphDetail::BufferResourceDesc(identity, "Ray Scene Buffer"));
        if(!importedBufferResult)
            return MakeUnexpected(Failure{});
        importedBuffers[index] = *importedBufferResult;
    }

    // Shared consumers reuse this identity. Reimporting the same TLAS returns it, keeping producer and cross-effect deps.
    const auto tlasResult = graph.importAccelStruct(
        resources.sceneTlas,
        RendererTaskGraphDetail::AccelStructResourceDesc(Name(s_SceneTlasResourceName), s_SceneTlasResourceLabel)
            .setInitialState(tlasInitialState)
    );
    if(!tlasResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphResourceId tlas = *tlasResult;

    auto opticalResult = ImportRayTracingOpticalSceneBuffer(resources.opticalScene, priorReadersDrained, graph, scratchArena);
    if(!opticalResult)
        return MakeUnexpected(Failure{});
    const RayTracingOpticalSceneGraphBuffer optical = Move(*opticalResult);

    RayTracingSceneGraphReads result(scratchArena);
    result.uses[0] = RendererTaskGraphDetail::ReadUse(tlas, Core::ResourceStates::AccelStructRead);
    for(usize index = 0u; index < s_SceneImportedBufferCount; ++index){
        const Core::ResourceStates::Mask state = index == 0u ? Core::ResourceStates::ConstantBuffer : Core::ResourceStates::ShaderResource;
        result.uses[index + 1u] = RendererTaskGraphDetail::ReadUse(importedBuffers[index], state);
    }
    result.uses[LengthOf(result.uses) - 1u] = RendererTaskGraphDetail::ReadUse(optical.resource);
    if(resources.csgTraceContextBuffer){
        result.csgUses.reserve(1u + resources.csgTraceBoundsBuffers.size());
        if(!RendererFramePipelineDetail::AppendCurrentCsgRayGeometry(
            resources.csgTraceContextBuffer,
            resources.csgTraceBoundsBuffers,
            graph,
            result.csgUses
        ))
            return MakeUnexpected(Failure{});
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererFramePipelineDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AppendRayTracingSceneShadowBuffers(
    const RayTracingDeferredGraphResourceSnapshot& rayTracingResources,
    Core::GpuTaskGraph& graph,
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses
){
    const Array<ReferenceWrapper<const Core::BufferHandle>, s_SceneShadowBufferCount> sceneShadowBuffers = {
        rayTracingResources.sceneBvhNodeBuffer,
        rayTracingResources.sceneInstanceBuffer,
        rayTracingResources.shadowInstanceMaterialBuffer,
        rayTracingResources.shadowMaterialTypedBuffer,
        rayTracingResources.shadowInstanceBuffer,
    };
    const Array<Name, s_SceneShadowBufferCount> sceneShadowIdentities = {
        Name(s_SceneBvhNodesName),
        Name(s_SceneInstancesName),
        Name(s_SceneShadowInstanceMaterialName),
        Name(s_SceneShadowMaterialTypedName),
        Name(s_SceneShadowInstancesName),
    };
    const Array<AStringView, s_SceneShadowBufferCount> sceneShadowLabels = {
        s_SceneBvhNodesLabel,
        s_SceneInstancesLabel,
        s_SceneShadowInstanceMaterialLabel,
        s_SceneShadowMaterialTypedLabel,
        s_SceneShadowInstancesLabel,
    };
    for(usize bufferIndex = 0u; bufferIndex < s_SceneShadowBufferCount; ++bufferIndex){
        const Core::BufferHandle& buffer = sceneShadowBuffers[bufferIndex];
        if(!buffer)
            continue;
        const auto resourceResult = RendererTaskGraphDetail::ImportExternalBuffer(graph, buffer, sceneShadowIdentities[bufferIndex], sceneShadowLabels[bufferIndex]);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        resourceUses.push_back(RendererTaskGraphDetail::ReadUse(resource, Core::ResourceStates::ShaderResource));
    }
    return true;
}

b8 AppendCurrentCsgRayGeometry(
    const Core::BufferHandle& context,
    const Span<const Core::BufferHandle> bounds,
    Core::GpuTaskGraph& graph,
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses
){
    if(!context)
        return true;
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
            const auto importedResource = RendererTaskGraphDetail::ImportExternalBuffer(graph, buffer, buffer->getCreationDescription().debugName, "Current CSG Ray Geometry");
            if(!importedResource)
                return false;
            resource = *importedResource;
        }
        if(!resource.valid())
            return false;
        resourceUses.push_back(RendererTaskGraphDetail::ReadUse(resource));
        return true;
    };
    if(!appendRead(context))
        return false;
    for(const Core::BufferHandle& boundBuffer : bounds){
        if(!appendRead(boundBuffer))
            return false;
    }
    return true;
}

TraceResourceSetUses MakeTraceResourceSetUses(const Core::GpuGraphResourceSetId traceGeometrySet, const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet)noexcept{
    TraceResourceSetUses result;
    if(traceGeometrySet.valid())
        result.uses[result.useCount++] = Core::GpuTaskResourceSetUse{ .resourceSet = traceGeometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    if(traceMaterialSampledTextureSet.valid())
        result.uses[result.useCount++] = Core::GpuTaskResourceSetUse{ .resourceSet = traceMaterialSampledTextureSet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

