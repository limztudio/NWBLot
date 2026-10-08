// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_graph_software_bvh_build_state.h"

#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FrameGraphSoftwareBvhBuildStateResult::FrameGraphSoftwareBvhBuildStateResult(Core::Alloc::ScratchArena& scratchArena)
    : buildStateResources(scratchArena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FrameGraphSoftwareBvhBuildStateImporter::FrameGraphSoftwareBvhBuildStateImporter(
    Core::GpuTaskGraph& graph,
    RendererMeshSystem& meshSystem
)
    : m_graph(graph)
    , m_meshSystem(meshSystem){
}


Expected<FrameGraphSoftwareBvhBuildStateResult> FrameGraphSoftwareBvhBuildStateImporter::declare(
    const FrameGraphSoftwareBvhBuildStateInputs& inputs,
    Core::Alloc::ScratchArena& scratchArena
){
    FrameGraphSoftwareBvhBuildStateResult result(scratchArena);
    using namespace RendererTaskGraphDetail;
    const RayTracingShadowPreparationResourceSnapshot& rayTracingShadowResources = *inputs.rayTracingShadowResources;
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> softwareBvhBuildStateResources{ scratchArena };
    Vector<Core::Buffer*, Core::Alloc::ScratchArena> softwareBvhBuildStateBuffers{ scratchArena };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_graph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    if(inputs.softwareTraceResourcesPrepared){
        const auto meshSoftwareBvhParentBuildStates = m_meshSystem.collectSoftwareBvhParentBuildStates(scratchArena);
        if(!meshSoftwareBvhParentBuildStates)
            return MakeUnexpected(Failure{});
        softwareBvhBuildStateResources.reserve(meshSoftwareBvhParentBuildStates->size() + 3u);
        softwareBvhBuildStateBuffers.reserve(meshSoftwareBvhParentBuildStates->size() + 3u);
        const auto appendSoftwareBvhBuildState = [&](
            const Core::BufferHandle& buffer,
            const Name identity,
            const AStringView label
        ){
            if(!buffer || !identity)
                return false;
            for(Core::Buffer* const existing : softwareBvhBuildStateBuffers){
                if(existing == buffer.get())
                    return true;
            }
            const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
            if(!resource.valid())
                return false;
            softwareBvhBuildStateBuffers.push_back(buffer.get());
            softwareBvhBuildStateResources.push_back(resource);
            return true;
        };
        for(const ECSRenderDetail::MeshSoftwareBvhParentBuildState& state : *meshSoftwareBvhParentBuildStates){
            if(!appendSoftwareBvhBuildState(state.buffer, state.identity, "Software BVH Parent")){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import software BVH parent build state"));
                return MakeUnexpected(Failure{});
            }
        }
        if(
            !rayTracingShadowResources.bvhSortKeysBuffer
            || !rayTracingShadowResources.bvhSortPayloadBuffer
            || !rayTracingShadowResources.bvhVisitCounterBuffer
            || !appendSoftwareBvhBuildState(
                rayTracingShadowResources.bvhSortKeysBuffer,
                Name("render.shadow_trace.sw_bvh_sort_keys"),
                "Software BVH Sort Keys"
            )
            || !appendSoftwareBvhBuildState(
                rayTracingShadowResources.bvhSortPayloadBuffer,
                Name("render.shadow_trace.sw_bvh_sort_payload"),
                "Software BVH Sort Payload"
            )
            || !appendSoftwareBvhBuildState(
                rayTracingShadowResources.bvhVisitCounterBuffer,
                Name("render.shadow_trace.sw_bvh_visit_counter"),
                "Software BVH Visit Counter"
            )
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import shared software BVH build state"));
            return MakeUnexpected(Failure{});
        }
    }
    result.buildStateResources = Move(softwareBvhBuildStateResources);
    result.declared = true;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

