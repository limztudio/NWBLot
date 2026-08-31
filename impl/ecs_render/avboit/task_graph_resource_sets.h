// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/avboit/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool GatherAvboitAliasFreeComputeEmulationResourceSet(
    Core::GpuTaskGraph& graph,
    const ECSRenderDetail::AvboitAliasFreeComputeEmulationGraphPlan& plan,
    Core::Alloc::ScratchArena& scratchArena,
    const Name& identity,
    const AStringView label,
    Core::GpuGraphResourceSetId& outResourceSet
){
    outResourceSet = {};
    if(!plan.captured || plan.outputBuffers.empty())
        return false;

    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> members{ scratchArena };
    members.reserve(plan.outputBuffers.size());
    for(const Core::BufferHandle& buffer : plan.outputBuffers){
        if(!buffer)
            return false;
        Core::GpuGraphResourceId resource = graph.findImportedBuffer(buffer);
        if(!resource.valid()){
            const Name bufferIdentity = buffer->getCreationDescription().debugName;
            if(!bufferIdentity)
                return false;
            resource = graph.importBuffer(buffer, BufferResourceDesc(bufferIdentity, label));
        }
        if(!resource.valid())
            return false;
        members.push_back(resource);
    }
    outResourceSet = graph.importResourceSet(
        Core::GpuGraphResourceSetDesc{}
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setMembers(members.data(), members.size())
    );
    return outResourceSet.valid();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

