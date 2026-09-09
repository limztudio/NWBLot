// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_scene_resources.h"

#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingSceneGraphReads ImportRayTracingSceneGraphReads(
    Core::GpuTaskGraph& graph,
    const RayTracingSceneGraphResources& resources,
    const Core::ResourceStates::Mask tlasInitialState){
    if(!resources.valid())
        return {};

    const Core::BufferHandle buffers[] = {
        resources.materialContextSlotsBuffer,
        resources.instanceMaterialBuffer,
        resources.materialTypedBuffer,
        resources.instanceBuffer,
    };
    Core::GpuGraphResourceId importedBuffers[LengthOf(buffers)] = {};
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return {};
        for(usize index = 0u; index < LengthOf(buffers); ++index)
            importedBuffers[index] = declarations.findImportedBuffer(buffers[index]);
    }
    for(usize index = 0u; index < LengthOf(buffers); ++index){
        if(importedBuffers[index].valid())
            continue;
        const Name identity = buffers[index]->getCreationDescription().debugName;
        if(!identity)
            return {};
        importedBuffers[index] = graph.importBuffer(
            buffers[index], RendererTaskGraphDetail::BufferResourceDesc(identity, "Ray Scene Buffer")
        );
        if(!importedBuffers[index].valid())
            return {};
    }

    // Other shared scene consumers use this identity. Reimporting the same TLAS with matching metadata returns its
    // existing graph identity, preserving the preparation producer and cross-effect read dependencies.
    const Core::GpuGraphResourceId tlas = graph.importAccelStruct(
        resources.sceneTlas,
        RendererTaskGraphDetail::AccelStructResourceDesc(Name("render.deferred_effects.tlas"), "Scene TLAS")
            .setInitialState(tlasInitialState)
    );
    if(!tlas.valid())
        return {};

    RayTracingSceneGraphReads result;
    result.uses[0] = RendererTaskGraphDetail::ReadUse(tlas, Core::ResourceStates::AccelStructRead);
    for(usize index = 0u; index < LengthOf(buffers); ++index){
        const Core::ResourceStates::Mask state = index == 0u
            ? Core::ResourceStates::ConstantBuffer : Core::ResourceStates::ShaderResource;
        result.uses[index + 1u] = RendererTaskGraphDetail::ReadUse(importedBuffers[index], state);
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

