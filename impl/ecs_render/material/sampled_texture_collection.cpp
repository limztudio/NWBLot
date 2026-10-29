// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sampled_texture_collection.h"

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AppendPreparedMaterialSurfaceSampledTextures(const MaterialSurfaceInfo& materialInfo, const RendererMaterialResourceState& resources, MaterialSampledTextureCollector<Core::Alloc::ScratchArena>& collector){
    if(!materialInfo.resourceReferencesResolved)
        return false;
    for(const MaterialResourceReference& resourceReference : materialInfo.resourceReferences){
        switch(resourceReference.resourceKind){
        case MaterialResourceKind::SampledImage2D:{
            // Resolved references own valid 2D cache entries until release clears the flag.
            NWB_ASSERT(resourceReference.textureAsset.valid());
            const auto foundTexture = resources.textureAssetCache.find(resourceReference.textureAsset.name());
            NWB_ASSERT(foundTexture != resources.textureAssetCache.end() && foundTexture.value());

            const TextureGpuResource& textureResource = *foundTexture.value();
            NWB_ASSERT(textureResource.valid() && textureResource.sampledImageHeapHandle.descriptorClass() == Core::GpuDescriptorClass::SampledImage);

            collector.append(textureResource.texture);
            break;
        }
        case MaterialResourceKind::Sampler:
            break;
        default:
            NWB_ASSERT(false);
            return false;
        }
    }
    return true;
}

Expected<Vector<Core::TextureHandle, Core::Alloc::ScratchArena>> GatherPreparedMaterialPassSampledTextures(
    const MaterialSurfaceInfoMap& materials,
    const RendererMaterialResourceState& resources,
    const Span<const MaterialPassDrawItemsView> drawItemSets,
    Core::Alloc::ScratchArena& scratchArena
){
    Vector<Core::TextureHandle, Core::Alloc::ScratchArena> textures(scratchArena);

    MaterialSampledTextureCollector<Core::Alloc::ScratchArena> collector(textures, scratchArena);
    const auto appendDrawItem = [&](const MaterialPassDrawItem& drawItem){
        const auto foundMaterial = materials.find(drawItem.pipelineKey.material);
        return foundMaterial != materials.end() && AppendPreparedMaterialSurfaceSampledTextures(foundMaterial.value(), resources, collector);
    };

    for(const MaterialPassDrawItemsView& drawItems : drawItemSets){
        for(const MaterialPassDrawItem& drawItem : drawItems.meshDrawItems){
            if(!appendDrawItem(drawItem))
                return MakeUnexpected(Failure{});
        }
        for(const MaterialPassDrawItem& drawItem : drawItems.indexedDrawItems){
            if(!appendDrawItem(drawItem))
                return MakeUnexpected(Failure{});
        }
        for(const MaterialPassDrawItem& drawItem : drawItems.computeDrawItems){
            if(!appendDrawItem(drawItem))
                return MakeUnexpected(Failure{});
        }
    }
    return textures;
}

b8 MergePreparedShadowMaterialSampledTextures(const Span<const Core::TextureHandle> sampledTextures, MaterialSampledTextureCollector<Core::Alloc::GlobalArena>& collector){
    for(const Core::TextureHandle& texture : sampledTextures){
        if(!texture || !texture->getCreationDescription().name)
            return false;
        collector.append(texture);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShadowMaterialSampledTextureCollector::ShadowMaterialSampledTextureCollector(Vector<Core::TextureHandle, Core::Alloc::GlobalArena>& textures, Core::Alloc::ScratchArena& scratchArena)
    : m_output(textures, scratchArena)
    , m_scratchArena(scratchArena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

