// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sampled_texture_graph_resources.h"

#include <core/graphics/vulkan/backend.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SampledTextureImportResult::Enum ImportMaterialSampledTextureResources(
    Core::GpuTaskGraph& graph,
    const Core::TextureHandle* const textures,
    const usize textureCount,
    const AStringView markerLabel,
    Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>& outResources){
    for(usize textureIndex = 0u; textureIndex < textureCount; ++textureIndex){
        const Core::TextureHandle& texture = textures[textureIndex];
        Core::GpuGraphResourceId resource;
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            if(!declarations.valid())
                return SampledTextureImportResult::GraphUnavailable;
            resource = declarations.findImportedTexture(texture);
        }
        if(!resource.valid()){
            if(!texture)
                return SampledTextureImportResult::MissingIdentity;
            const Name identity = texture->getCreationDescription().name;
            if(!identity)
                return SampledTextureImportResult::MissingIdentity;
            resource = graph.importTexture(texture, RendererTaskGraphDetail::TextureResourceDesc(identity, markerLabel));
            if(!resource.valid())
                return SampledTextureImportResult::ImportFailed;
        }
        outResources.push_back(resource);
    }
    return SampledTextureImportResult::Success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

