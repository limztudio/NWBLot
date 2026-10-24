// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sampled_texture_graph_resources.h"

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>, SampledTextureImportFailure::Enum> ImportMaterialSampledTextureResources(
    const Span<const Core::TextureHandle> textures,
    const AStringView markerLabel,
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena
){
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena> resources(scratchArena);
    resources.reserve(textures.size());
    for(const Core::TextureHandle& texture : textures){
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
            const auto resourceResult = graph.importTexture(texture, RendererTaskGraphDetail::TextureResourceDesc(identity, markerLabel));
            if(!resourceResult){
                resource = {};
                return MakeUnexpected(SampledTextureImportFailure::ImportFailed);
            }
            resource = *resourceResult;
        }
        resources.push_back(resource);
    }
    return resources;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

