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


Expected<RayTracingSceneGraphReads> ImportRayTracingSceneGraphReads(Core::GpuTaskGraph& graph, const RayTracingSceneGraphResources& resources, const Core::ResourceStates::Mask tlasInitialState, Core::Alloc::ScratchArena& scratchArena){
    if(!resources.valid())
        return MakeUnexpected(Failure{});

    const ReferenceWrapper<const Core::BufferHandle> buffers[] = { resources.materialContextSlotsBuffer, resources.instanceMaterialBuffer, resources.materialTypedBuffer, resources.instanceBuffer, };
    Core::GpuGraphResourceId importedBuffers[LengthOf(buffers)] = {};
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return MakeUnexpected(Failure{});
        for(usize index = 0u; index < LengthOf(buffers); ++index)
            importedBuffers[index] = declarations.findImportedBuffer(buffers[index]);
    }
    for(usize index = 0u; index < LengthOf(buffers); ++index){
        if(importedBuffers[index].valid())
            continue;
        const Core::BufferHandle& buffer = buffers[index];
        const Name identity = buffer->getCreationDescription().debugName;
        if(!identity)
            return MakeUnexpected(Failure{});
        const auto importedBuffersIndexResult = graph.importBuffer(buffer, RendererTaskGraphDetail::BufferResourceDesc(identity, "Ray Scene Buffer"));
        if(!importedBuffersIndexResult){
            importedBuffers[index] = {};
            return MakeUnexpected(Failure{});
        }
        importedBuffers[index] = *importedBuffersIndexResult;
    }

    // Other shared scene consumers use this identity. Reimporting the same TLAS with matching metadata returns its existing graph identity, preserving the preparation producer and cross-effect read dependencies.
    const auto tlasResult = graph.importAccelStruct(
        resources.sceneTlas,
        RendererTaskGraphDetail::AccelStructResourceDesc(Name(RendererFramePipelineDetail::s_SceneTlasResourceName), RendererFramePipelineDetail::s_SceneTlasResourceLabel)
            .setInitialState(tlasInitialState)
    );
    if(!tlasResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphResourceId tlas = *tlasResult;

    auto opticalResult = ImportRayTracingOpticalSceneBuffer(graph, resources.opticalScene, scratchArena);
    if(!opticalResult)
        return MakeUnexpected(Failure{});
    const RayTracingOpticalSceneGraphBuffer optical = Move(*opticalResult);

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
            return MakeUnexpected(Failure{});
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

