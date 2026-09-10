// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_mesh_types.h"

#include <core/alloc/scratch.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_mesh/runtime/mesh_requests.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Release geometry before its cache entry is erased, in iteration order.
template<typename ReleaseMesh>
void PruneRuntimeMeshResources(
    HashMap<Name, MeshResources, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena>& meshes,
    const MeshSystem* meshSystem,
    ReleaseMesh&& releaseMesh,
    Core::Alloc::ScratchArena& scratchArena
){
    Optional<RuntimeMeshRequestSet> requests;
    if(meshSystem){
        usize runtimeMeshCount = 0u;
        for(const auto& entry : meshes){
            if(entry.second.runtimeMesh)
                ++runtimeMeshCount;
        }
        if(runtimeMeshCount == 0u)
            return;
        requests.emplace(scratchArena, runtimeMeshCount);
        for(const auto& entry : meshes){
            const MeshResources& mesh = entry.second;
            if(mesh.runtimeMesh)
                requests->add(mesh.meshName, mesh.runtimeMeshVersion);
        }
        meshSystem->markLiveRuntimeMeshes(*requests);
    }
    for(auto it = meshes.begin(); it != meshes.end();){
        const MeshResources& mesh = it.value();
        if(!mesh.runtimeMesh){
            ++it;
            continue;
        }

        if(requests && requests->containsLive(mesh.meshName, mesh.runtimeMeshVersion)){
            ++it;
            continue;
        }

        releaseMesh(it.value());
        it = meshes.erase(it);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

