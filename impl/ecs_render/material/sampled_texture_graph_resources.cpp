// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sampled_texture_graph_resources.h"

#include <core/graphics/backend_selection/backend.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>, SampledTextureImportFailure::Enum> ImportMaterialSampledTextureResources(
    Core::GpuTaskGraph& graph,
    const Core::TextureHandle* const textures,
    const usize textureCount,
    const AStringView markerLabel,
    Core::Alloc::ScratchArena& scratchArena
){
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> resources(scratchArena);
    resources.reserve(textureCount);
    for(usize textureIndex = 0u; textureIndex < textureCount; ++textureIndex){
        const Core::TextureHandle& texture = textures[textureIndex];
        Core::GpuGraphResourceId resource;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            if(!declarations.valid())
                return MakeUnexpected(SampledTextureImportFailure::GraphUnavailable);
            resource = declarations.findImportedTexture(texture);
        }
        if(!resource.valid()){
            if(!texture)
                return MakeUnexpected(SampledTextureImportFailure::MissingIdentity);
            const Name identity = texture->getCreationDescription().name;
            if(!identity)
                return MakeUnexpected(SampledTextureImportFailure::MissingIdentity);
            resource = graph.importTexture(texture, RendererTaskGraphDetail::TextureResourceDesc(identity, markerLabel));
            if(!resource.valid())
                return MakeUnexpected(SampledTextureImportFailure::ImportFailed);
        }
        resources.push_back(resource);
    }
    return resources;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

