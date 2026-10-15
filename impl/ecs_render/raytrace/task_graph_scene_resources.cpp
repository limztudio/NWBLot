// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_scene_resources.h"
#include "task_graph_optical_scene_upload.h"

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/renderer_frame_pipeline_graph_shared.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingSceneGraphReads ImportRayTracingSceneGraphReads(Core::GpuTaskGraph& graph, const RayTracingSceneGraphResources& resources, const Core::ResourceStates::Mask tlasInitialState, Core::Alloc::ScratchArena& scratchArena){
    if(!resources.valid())
        return RayTracingSceneGraphReads(scratchArena);

    const ReferenceWrapper<const Core::BufferHandle> buffers[] = { resources.materialContextSlotsBuffer, resources.instanceMaterialBuffer, resources.materialTypedBuffer, resources.instanceBuffer, };
    Core::GpuGraphResourceId importedBuffers[LengthOf(buffers)] = {};
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return RayTracingSceneGraphReads(scratchArena);
        for(usize index = 0u; index < LengthOf(buffers); ++index)
            importedBuffers[index] = declarations.findImportedBuffer(buffers[index]);
    }
    for(usize index = 0u; index < LengthOf(buffers); ++index){
        if(importedBuffers[index].valid())
            continue;
        const Core::BufferHandle& buffer = buffers[index];
        const Name identity = buffer->getCreationDescription().debugName;
        if(!identity)
            return RayTracingSceneGraphReads(scratchArena);
        importedBuffers[index] = graph.importBuffer(buffer, RendererTaskGraphDetail::BufferResourceDesc(identity, "Ray Scene Buffer"));
        if(!importedBuffers[index].valid())
            return RayTracingSceneGraphReads(scratchArena);
    }

    // Other shared scene consumers use this identity. Reimporting the same TLAS with matching metadata returns its existing graph identity, preserving the preparation producer and cross-effect read dependencies.
    const Core::GpuGraphResourceId tlas = graph.importAccelStruct(
        resources.sceneTlas,
        RendererTaskGraphDetail::AccelStructResourceDesc(Name(RendererFramePipelineDetail::s_SceneTlasResourceName), RendererFramePipelineDetail::s_SceneTlasResourceLabel)
            .setInitialState(tlasInitialState)
    );
    if(!tlas.valid())
        return RayTracingSceneGraphReads(scratchArena);

    const RayTracingOpticalSceneGraphBuffer optical = ImportRayTracingOpticalSceneBuffer(graph, resources.opticalScene, scratchArena);
    if(!optical.valid())
        return RayTracingSceneGraphReads(scratchArena);

    RayTracingSceneGraphReads result(scratchArena);
    result.uses[0] = RendererTaskGraphDetail::ReadUse(tlas, Core::ResourceStates::AccelStructRead);
    for(usize index = 0u; index < LengthOf(buffers); ++index){
        const Core::ResourceStates::Mask state = index == 0u ? Core::ResourceStates::ConstantBuffer : Core::ResourceStates::ShaderResource;
        result.uses[index + 1u] = RendererTaskGraphDetail::ReadUse(importedBuffers[index], state);
    }
    result.uses[LengthOf(result.uses) - 1u] = RendererTaskGraphDetail::ReadUse(optical.resource);
    if(resources.csgTraceContextBuffer){
        result.csgUses.reserve(1u + resources.csgTraceBoundsBufferCount);
        if(!RendererFramePipelineDetail::AppendCurrentCsgRayGeometry(
            graph, resources.csgTraceContextBuffer, resources.csgTraceBoundsBuffers, resources.csgTraceBoundsBufferCount,
            [&](const Core::GpuGraphResourceId resource){ result.csgUses.push_back(RendererTaskGraphDetail::ReadUse(resource)); }
        ))
            return RayTracingSceneGraphReads(scratchArena);
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

