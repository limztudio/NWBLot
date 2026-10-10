// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "scene_resources.h"

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RayTracingSceneGraphReads{
    [[nodiscard]] bool valid()const noexcept{
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


    // Ordinary scene reads remain inline; only admitted CSG work allocates the variable posed-bounds list.
    Core::GpuTaskResourceUse uses[6] = {};
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgUses;
};

// Import common scene reads once per physical resource. Geometry buffers and material sampled textures belong to
// the existing frozen resource sets supplied separately by the caller. A failure returns an entirely invalid bundle.
[[nodiscard]] RayTracingSceneGraphReads ImportRayTracingSceneGraphReads(
    Core::GpuTaskGraph& graph,
    const RayTracingSceneGraphResources& resources,
    Core::ResourceStates::Mask tlasInitialState,
    Core::Alloc::ScratchArena& scratchArena
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

