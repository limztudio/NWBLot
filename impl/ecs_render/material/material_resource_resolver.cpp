// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_resource_resolver.h"

#include <impl/ecs_render/material/material_instance_override_applier.adhd.h>
#include <impl/ecs_render/shader/shader_system.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_surface{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CacheT, typename ReleaseFn>
static void ReleaseAssetCache(CacheT& cache, ReleaseFn&& releaseItem)noexcept(
    []()constexpr noexcept{
        using Returned = decltype(DeclVal<CacheT&>().begin());
        using Iterator = Decay_T<Returned>;
        return
            noexcept(DeclVal<CacheT&>().begin())
            && (!IsReference_V<Returned> || IsNothrowConvertible_V<Returned, Iterator>)
            && IsNothrowDestructible_V<Iterator>
            && noexcept(static_cast<b8>(DeclVal<Iterator&>() != DeclVal<CacheT&>().end()))
            && noexcept(++DeclVal<Iterator&>())
#if NWB_OCCUR_ASSERT
            && noexcept(static_cast<b8>(!DeclVal<Iterator&>().value()))
#endif
            && noexcept(DeclVal<ReleaseFn&>()(*DeclVal<Iterator&>().value()))
            && noexcept(DeclVal<CacheT&>().clear())
        ;
    }()
){
    for(auto it = cache.begin(); it != cache.end(); ++it){
        NWB_ASSERT(it.value());
        releaseItem(*it.value());
    }
    cache.clear();
}

static void ReleaseTextureAssetCache(Core::GraphicsRuntime& graphics, RendererMaterialResourceState& resources){
    ReleaseAssetCache(resources.textureAssetCache, [&](TextureGpuResource& resource){ TextureAssetLoader::Release(graphics, resource); });
}

static void ReleaseSamplerAssetCache(Core::GraphicsRuntime& graphics, RendererMaterialResourceState& resources){
    ReleaseAssetCache(resources.samplerAssetCache, [&](SamplerGpuResource& resource){ SamplerAssetLoader::Release(graphics, resource); });
}

static void ReleaseMaterialResourceState(Core::GraphicsRuntime& graphics, RendererMaterialResourceState& resources){
    ReleaseTextureAssetCache(graphics, resources);
    ReleaseSamplerAssetCache(graphics, resources);
}

template<typename AssetT, typename ResourceT, typename CacheT, typename LoadFn>
[[nodiscard]] static Expected<NotNull<ResourceT*>> FindOrCreateCachedAsset(CacheT& cache, const Core::Assets::AssetRef<AssetT>& assetRef, Core::GpuDescriptorHeap& heap, Core::GpuDescriptorHandle ResourceT::* descriptorMember, LoadFn&& loadResource){
    NWB_ASSERT(assetRef.valid());
    const Name& assetPath = assetRef.name();
    auto assetIt = cache.find(assetPath);
    if(assetIt == cache.end()){
        UniquePtr<ResourceT> resource = MakeUnique<ResourceT>();
        Core::GpuDescriptorRetirementGuard retireLoading(heap, resource.get()->*descriptorMember);
        if(!loadResource(*resource, assetRef, assetPath))
            return MakeUnexpected(Failure{});
        const Core::GpuDescriptorHandle pendingDescriptor = resource.get()->*descriptorMember;
        Core::GpuDescriptorRetirementGuard retireUnpublished(heap, pendingDescriptor);
        retireLoading.release();
        auto insertResult = cache.try_emplace(assetPath, Move(resource));
        assetIt = insertResult.first;
        if(insertResult.second)
            retireUnpublished.release();
    }
    NWB_ASSERT(assetIt.value());
    return MakeNotNull(assetIt.value().get());
}

[[nodiscard]] static Expected<u32> ResolveTextureAssetSlot(RendererMaterialResourceState& resources, const Core::Assets::AssetRef<Texture>& textureAsset, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager){
    const auto textureResourceResult = FindOrCreateCachedAsset<Texture, TextureGpuResource>(
        resources.textureAssetCache,
        textureAsset,
        graphics.getDevice().getDescriptorHeap(),
        &TextureGpuResource::sampledImageHeapHandle,
        [&](TextureGpuResource& outResource, const Core::Assets::AssetRef<Texture>& assetRef, const Name& assetPath){
            if(!TextureAssetLoader::Load(assetRef, assetPath, s_RendererAssetOwnerName, graphics, assetManager, outResource))
                return false;
            if(outResource.sampledImageHeapHandle.descriptorClass() != Core::GpuDescriptorClass::SampledImage){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: Texture2D asset '{}' has an incompatible texture dimension"), ConvertString(assetPath.resolvedText()));
                return false;
            }
            return true;
        }
    );
    if(!textureResourceResult)
        return MakeUnexpected(textureResourceResult.error());
    const NotNull<TextureGpuResource*> textureResource = *textureResourceResult;
    NWB_ASSERT(textureResource->valid() && textureResource->sampledImageHeapHandle.descriptorClass() == Core::GpuDescriptorClass::SampledImage);
    return textureResource->sampledImageHeapHandle.slot();
}

[[nodiscard]] static Expected<u32> ResolveSamplerAssetSlot(RendererMaterialResourceState& resources, const Core::Assets::AssetRef<Sampler>& samplerAsset, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager){
    const auto samplerResourceResult = FindOrCreateCachedAsset<Sampler, SamplerGpuResource>(
        resources.samplerAssetCache,
        samplerAsset,
        graphics.getDevice().getDescriptorHeap(),
        &SamplerGpuResource::samplerHeapHandle,
        [&](SamplerGpuResource& outResource, const Core::Assets::AssetRef<Sampler>& assetRef, const Name& assetPath){
            return SamplerAssetLoader::Load(assetRef, assetPath, s_RendererAssetOwnerName, graphics, assetManager, outResource);
        }
    );
    if(!samplerResourceResult)
        return MakeUnexpected(samplerResourceResult.error());
    const NotNull<SamplerGpuResource*> samplerResource = *samplerResourceResult;
    NWB_ASSERT(samplerResource->valid() && samplerResource->samplerHeapHandle.descriptorClass() == Core::GpuDescriptorClass::Sampler);
    return samplerResource->samplerHeapHandle.slot();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererMaterialResourceResolver::Resolve(MaterialSurfaceInfo& materialInfo, RendererMaterialResourceState& resources, Core::GraphicsRuntime& graphicsModule, Core::Assets::AssetManager& assetManager){
    if(materialInfo.resourceReferencesResolved)
        return true;

    materialInfo.constantTypedBytes = materialInfo.unpatchedConstantTypedBytes;
    if(materialInfo.resourceReferences.empty()){
        materialInfo.resourceReferencesResolved = true;
        return true;
    }

    Core::GpuDescriptorHeap& heap = graphicsModule.getDevice().getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cannot resolve material resources without an initialized descriptor heap"));
        return false;
    }

    for(const MaterialResourceReference& resourceReference : materialInfo.resourceReferences){
        Expected<u32> heapSlot = MakeUnexpected(Failure{});
        switch(resourceReference.resourceKind){
        case MaterialResourceKind::SampledImage2D:
            heapSlot = __hidden_material_surface::ResolveTextureAssetSlot(resources, resourceReference.textureAsset, graphicsModule, assetManager);
            if(!heapSlot){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' failed to load Texture2D asset '{}'"), ConvertString(materialInfo.materialName.resolvedText()), ConvertString(resourceReference.textureAsset.name().resolvedText()));
                return false;
            }
            break;
        case MaterialResourceKind::Sampler:
            heapSlot = __hidden_material_surface::ResolveSamplerAssetSlot(resources, resourceReference.samplerAsset, graphicsModule, assetManager);
            if(!heapSlot){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' failed to load sampler asset '{}'"), ConvertString(materialInfo.materialName.resolvedText()), ConvertString(resourceReference.samplerAsset.name().resolvedText()));
                return false;
            }
            break;
        default:
            NWB_ASSERT(false);
            return false;
        }

        MaterialInstanceOverrideApplier().patchConstantResourceSlot(resourceReference.constantByteOffset, *heapSlot, materialInfo.constantTypedBytes);
    }

    materialInfo.resourceReferencesResolved = true;
    return true;
}

void RendererMaterialResourceResolver::Release(Core::GraphicsRuntime& graphics, RendererMaterialResourceState& resources){
    __hidden_material_surface::ReleaseMaterialResourceState(graphics, resources);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
