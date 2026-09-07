// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sampled_texture_collection.h"

#include <core/graphics/vulkan/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AppendPreparedMaterialSurfaceSampledTextures(
    const MaterialSurfaceInfo& materialInfo,
    const RendererMaterialResourceState& resources,
    MaterialSampledTextureCollector<Core::Alloc::ScratchArena>& collector){
    if(!materialInfo.resourceReferencesResolved)
        return false;
    for(const MaterialResourceReference& resourceReference : materialInfo.resourceReferences){
        switch(resourceReference.resourceKind){
        case MaterialResourceKind::SampledImage2D:{
            if(
                resourceReference.resourceSource != MaterialResourceSource::Asset
                || !resourceReference.textureAsset.valid()
            )
                return false;

            const auto foundTexture = resources.textureAssetCache.find(resourceReference.textureAsset.name());
            if(foundTexture == resources.textureAssetCache.end() || !foundTexture.value())
                return false;

            const TextureGpuResource& textureResource = *foundTexture.value();
            if(
                !textureResource.valid()
                || textureResource.sampledImageHeapHandle.descriptorClass() != Core::GpuDescriptorClass::SampledImage
                || !textureResource.texture
            )
                return false;

            collector.append(textureResource.texture);
            break;
        }
        case MaterialResourceKind::Sampler:
            break;
        default:
            return false;
        }
    }
    return true;
}

bool GatherPreparedMaterialPassSampledTextures(
    const MaterialSurfaceInfoMap& materials,
    const RendererMaterialResourceState& resources,
    const MaterialPassDrawItems* const* const drawItemSets,
    const usize drawItemSetCount,
    Vector<Core::TextureHandle, Core::Alloc::ScratchArena>& outTextures,
    Core::Alloc::ScratchArena& scratchArena){
    outTextures.clear();
    if(drawItemSetCount != 0u && !drawItemSets)
        return false;

    MaterialSampledTextureCollector<Core::Alloc::ScratchArena> collector(outTextures, scratchArena);
    const auto appendDrawItem = [&](const MaterialPassDrawItem& drawItem){
        const auto foundMaterial = materials.find(drawItem.pipelineKey.material);
        return foundMaterial != materials.end()
            && AppendPreparedMaterialSurfaceSampledTextures(foundMaterial.value(), resources, collector)
        ;
    };

    for(usize drawItemSetIndex = 0u; drawItemSetIndex < drawItemSetCount; ++drawItemSetIndex){
        const MaterialPassDrawItems* const drawItems = drawItemSets[drawItemSetIndex];
        if(!drawItems)
            return false;
        for(const MaterialPassDrawItem& drawItem : drawItems->meshDrawItems){
            if(!appendDrawItem(drawItem))
                return false;
        }
        for(const MaterialPassDrawItem& drawItem : drawItems->computeDrawItems){
            if(!appendDrawItem(drawItem))
                return false;
        }
    }
    return true;
}

bool MergePreparedShadowMaterialSampledTextures(
    const Vector<Core::TextureHandle, Core::Alloc::ScratchArena>& sampledTextures,
    MaterialSampledTextureCollector<Core::Alloc::GlobalArena>& collector){
    for(const Core::TextureHandle& texture : sampledTextures){
        if(!texture || !texture->getCreationDescription().name)
            return false;
        collector.append(texture);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShadowMaterialSampledTextureCollector::ShadowMaterialSampledTextureCollector(
    Vector<Core::TextureHandle, Core::Alloc::GlobalArena>& textures,
    Core::Alloc::ScratchArena& scratchArena
)
    : m_output(textures, scratchArena)
    , m_scratchArena(scratchArena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

