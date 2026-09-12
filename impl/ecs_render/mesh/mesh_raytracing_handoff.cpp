// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_system.h"

#include "runtime_mesh_pruning.h"

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/mesh/renderer_mesh_state.h>

#include <core/alloc/scratch.h>
#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets_mesh/meshlet_triangle_indices.h>
#include <impl/assets_mesh/meshlet_vertex_attributes.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_mesh/runtime/buffer_upload.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererMeshSystem::confirmAcceptedRayTracingStateHandoffs()noexcept{
    for(auto meshIt = m_meshState.m_meshes.begin(); meshIt != m_meshState.m_meshes.end(); ++meshIt){
        MeshResources& mesh = meshIt.value();
        if(mesh.blasBackingFresh && mesh.blasBackingStateHandoffPending){
            mesh.blasBackingFresh = false;
            mesh.blasBackingStateHandoffPending = false;
        }
    }
}

void RendererMeshSystem::discardRayTracingBuildState()noexcept{
    for(auto meshIt = m_meshState.m_meshes.begin(); meshIt != m_meshState.m_meshes.end(); ++meshIt){
        MeshResources& mesh = meshIt.value();
        mesh.blasBackingStateHandoffPending = false;
        if(mesh.blas)
            mesh.blasBuildPending = true;
        if(mesh.swBvhNodeBuffer || mesh.swBvhParentBuffer){
            mesh.swBvhBuildPending = true;
            mesh.swBvhTopologyBuilt = false;
        }
        mesh.blasRefitsSinceRebuild = 0u;
        mesh.swBvhRefitsSinceRebuild = 0u;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

