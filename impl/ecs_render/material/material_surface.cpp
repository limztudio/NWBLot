// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"
#include "sampled_texture_collection.h"

#include <impl/assets_material/asset.h>
#include <impl/assets_shader/asset.h>
#include <impl/ecs_render/material/renderer_material_state.h>

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>
#include <impl/assets_sampler/loader.h>
#include <impl/assets_texture/loader.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_surface{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CacheT, typename ReleaseFn>
static void ReleaseAssetCache(CacheT& cache, ReleaseFn&& releaseItem){
    for(auto it = cache.begin(); it != cache.end(); ++it){
        if(it.value())
            releaseItem(*it.value());
    }
    cache.clear();
}

static void ReleaseTextureAssetCache(Core::GraphicsRuntime& graphics, RendererMaterialResourceState& resources){
    ReleaseAssetCache(resources.textureAssetCache, [&](TextureGpuResource& resource){ TextureAssetLoader::Release(resource, graphics); });
}

static void ReleaseSamplerAssetCache(Core::GraphicsRuntime& graphics, RendererMaterialResourceState& resources){
    ReleaseAssetCache(resources.samplerAssetCache, [&](SamplerGpuResource& resource){ SamplerAssetLoader::Release(resource, graphics); });
}

static void ReleaseMaterialResourceState(Core::GraphicsRuntime& graphics, RendererMaterialResourceState& resources){
    ReleaseTextureAssetCache(graphics, resources);
    ReleaseSamplerAssetCache(graphics, resources);
}

template<typename AssetT, typename ResourceT, typename CacheT, typename LoadFn, typename ReleaseFn>
[[nodiscard]] static ResourceT* FindOrCreateCachedAsset(
    CacheT& cache,
    const Core::Assets::AssetRef<AssetT>& assetRef,
    const AStringView emptyKindText,
    LoadFn&& loadResource,
    ReleaseFn&& releaseResource
){
    if(!assetRef.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material {} asset reference is empty"), StringConvert(emptyKindText));
        return nullptr;
    }

    const Name& assetPath = assetRef.name();
    auto assetIt = cache.find(assetPath);
    if(assetIt == cache.end()){
        UniquePtr<ResourceT> resource = MakeUnique<ResourceT>();
        if(!loadResource(*resource, assetRef, assetPath))
            return nullptr;

        auto insertResult = cache.try_emplace(assetPath, Move(resource));
        assetIt = insertResult.first;
        if(!insertResult.second && resource)
            releaseResource(*resource);
    }

    NWB_ASSERT(assetIt.value());
    return assetIt.value().get();
}

[[nodiscard]] static bool ResolveTextureAssetSlot(
    RendererMaterialResourceState& resources,
    const Core::Assets::AssetRef<Texture>& textureAsset,
    Core::GraphicsRuntime& graphics,
    Core::Assets::AssetManager& assetManager,
    u32& outHeapSlot
){
    outHeapSlot = 0u;
    TextureGpuResource* const textureResource = FindOrCreateCachedAsset<Texture, TextureGpuResource>(
        resources.textureAssetCache,
        textureAsset,
        "Texture2D",
        [&](TextureGpuResource& outResource, const Core::Assets::AssetRef<Texture>& assetRef, const Name& assetPath){
            if(!TextureAssetLoader::Load(
                outResource,
                assetRef,
                assetPath,
                graphics,
                assetManager,
                NWB_TEXT("RendererSystem")
            ))
                return false;
            if(outResource.sampledImageHeapHandle.descriptorClass() != Core::GpuDescriptorClass::SampledImage){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: Texture2D asset '{}' has an incompatible texture dimension")
                    , StringConvert(assetPath.resolvedText())
                );
                TextureAssetLoader::Release(outResource, graphics);
                return false;
            }
            return true;
        },
        [&](TextureGpuResource& liveResource){ TextureAssetLoader::Release(liveResource, graphics); }
    );
    if(!textureResource)
        return false;

    const Name& texturePath = textureAsset.name();
    if(!textureResource->valid() || textureResource->sampledImageHeapHandle.descriptorClass() != Core::GpuDescriptorClass::SampledImage){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cached Texture2D asset '{}' is invalid")
            , StringConvert(texturePath.resolvedText())
        );
        return false;
    }

    outHeapSlot = textureResource->sampledImageHeapHandle.slot();
    return true;
}

[[nodiscard]] static bool ResolveSamplerAssetSlot(
    RendererMaterialResourceState& resources,
    const Core::Assets::AssetRef<Sampler>& samplerAsset,
    Core::GraphicsRuntime& graphics,
    Core::Assets::AssetManager& assetManager,
    u32& outHeapSlot
){
    outHeapSlot = 0u;
    SamplerGpuResource* const samplerResource = FindOrCreateCachedAsset<Sampler, SamplerGpuResource>(
        resources.samplerAssetCache,
        samplerAsset,
        Sampler::s_AssetTypeText,
        [&](SamplerGpuResource& outResource, const Core::Assets::AssetRef<Sampler>& assetRef, const Name& assetPath){
            return SamplerAssetLoader::Load(
                outResource,
                assetRef,
                assetPath,
                graphics,
                assetManager,
                NWB_TEXT("RendererSystem")
            );
        },
        [&](SamplerGpuResource& liveResource){ SamplerAssetLoader::Release(liveResource, graphics); }
    );
    if(!samplerResource)
        return false;

    const Name& samplerPath = samplerAsset.name();
    if(
        !samplerResource->valid()
        || samplerResource->samplerHeapHandle.descriptorClass() != Core::GpuDescriptorClass::Sampler
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cached sampler asset '{}' is invalid"), StringConvert(samplerPath.resolvedText()));
        return false;
    }

    outHeapSlot = samplerResource->samplerHeapHandle.slot();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererMaterialSystem::resolveMaterialResourceReferences(MaterialSurfaceInfo& materialInfo){
    if(materialInfo.resourceReferencesResolved)
        return true;

    materialInfo.constantTypedBytes = materialInfo.unpatchedConstantTypedBytes;
    if(materialInfo.resourceReferences.empty()){
        materialInfo.resourceReferencesResolved = true;
        return true;
    }

    RendererMaterialResourceState& resources = m_materialState.m_resourceState;
    Core::GraphicsRuntime& graphicsModule = m_graphics;
    Core::GpuDescriptorHeap& heap = graphicsModule.getDevice().getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cannot resolve material resources without an initialized descriptor heap"));
        return false;
    }

    for(const MaterialResourceReference& resourceReference : materialInfo.resourceReferences){
        u32 heapSlot = 0u;
        switch(resourceReference.resourceKind){
        case MaterialResourceKind::SampledImage2D:
            if(!__hidden_material_surface::ResolveTextureAssetSlot(
                resources,
                resourceReference.textureAsset,
                graphicsModule,
                m_assetManager,
                heapSlot
            )){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' failed to load Texture2D asset '{}'")
                    , StringConvert(materialInfo.materialName.resolvedText())
                    , StringConvert(resourceReference.textureAsset.name().resolvedText())
                );
                return false;
            }
            break;
        case MaterialResourceKind::Sampler:
            if(!__hidden_material_surface::ResolveSamplerAssetSlot(
                resources,
                resourceReference.samplerAsset,
                graphicsModule,
                m_assetManager,
                heapSlot
            )){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' failed to load sampler asset '{}'")
                    , StringConvert(materialInfo.materialName.resolvedText())
                    , StringConvert(resourceReference.samplerAsset.name().resolvedText())
                );
                return false;
            }
            break;
        default:
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' has an invalid material resource kind"), StringConvert(materialInfo.materialName.resolvedText()));
            return false;
        }

        if(
            resourceReference.constantByteOffset > materialInfo.constantTypedBytes.size()
            || sizeof(heapSlot) > materialInfo.constantTypedBytes.size() - resourceReference.constantByteOffset
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' resource slot exceeds constant typed bytes"), StringConvert(materialInfo.materialName.resolvedText()));
            return false;
        }
        NWB_MEMCPY(
            materialInfo.constantTypedBytes.data() + resourceReference.constantByteOffset,
            materialInfo.constantTypedBytes.size() - resourceReference.constantByteOffset,
            &heapSlot,
            sizeof(heapSlot)
        );
    }

    materialInfo.resourceReferencesResolved = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererMaterialSystem::SplitMaterialTypedBytesByClass(
    const Material& material,
    MaterialTypedByteVector& outConstantTypedBytes,
    MaterialTypedByteVector& outMutableDefaultTypedBytes
){
    NWB_ASSERT(material.typedLayoutHash() != 0u);
    outConstantTypedBytes.clear();
    outMutableDefaultTypedBytes.clear();

    const auto& packedTypedBytes = material.typedBlockBytes();
    usize sourceByteOffset = 0u;
    for(const MaterialTypedLayoutBlock& block : material.typedLayoutBlocks()){
        // Material::loadBinary already ran ValidateMaterialTypedLayout; keep a debug-only invariant here.
        NWB_ASSERT(IsValidMaterialBlockClass(block.blockClass));
        NWB_ASSERT((block.byteSize & (sizeof(u32) - 1u)) == 0u);
        NWB_ASSERT(sourceByteOffset <= packedTypedBytes.size() && block.byteSize <= packedTypedBytes.size() - sourceByteOffset);

        MaterialTypedByteVector& targetTypedBytes = block.blockClass == MaterialBlockClass::MaterialConstant
            ? outConstantTypedBytes
            : outMutableDefaultTypedBytes
        ;
        targetTypedBytes.insert(
            targetTypedBytes.end(),
            packedTypedBytes.begin() + sourceByteOffset,
            packedTypedBytes.begin() + sourceByteOffset + block.byteSize
        );
        sourceByteOffset += block.byteSize;
    }
    // Material::loadBinary already validated the packed byte count against the cooked layout.
    NWB_ASSERT(sourceByteOffset == packedTypedBytes.size());
}

bool RendererMaterialSystem::createMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset, MaterialSurfaceInfo*& outInfo){
    outInfo = nullptr;

    const Name materialPath = materialAsset.name();
    if(!materialPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer material is empty"));
        return false;
    }

    const auto foundInfo = m_materialState.m_surfaceInfos.find(materialPath);
    if(foundInfo != m_materialState.m_surfaceInfos.end()){
        outInfo = &foundInfo.value();
        return resolveMaterialResourceReferences(*outInfo);
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    const Material* loadedMaterial = m_assetManager.loadTypedSync<Material>(
        materialPath,
        loadedAsset,
        NWB_TEXT("RendererSystem"),
        "material"
    );
    if(!loadedMaterial)
        return false;

    const Material& material = *loadedMaterial;

    MaterialSurfaceInfo createdInfo(m_arena);
    createdInfo.materialName = materialPath;
    // Material::loadBinary already rejected empty shader variants and missing material interfaces.
    NWB_ASSERT(!material.shaderVariant().empty());
    NWB_ASSERT(material.materialInterface());
    createdInfo.shaderVariant.reserve(material.shaderVariant().size());
    createdInfo.shaderVariant.assign(material.shaderVariant().data(), material.shaderVariant().size());

    const bool hasPixelShader = material.findShader(createdInfo.pixelShader);
    const bool hasMeshShader = material.findShader(createdInfo.meshShader);
    createdInfo.avboitAccumulatePixelShader = material.avboitAccumulatePixelShader();
    createdInfo.avboitOccupancyPixelShader = material.avboitOccupancyPixelShader();
    createdInfo.avboitExtinctionPixelShader = material.avboitExtinctionPixelShader();
    if(!hasMeshShader){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' is missing required mesh shader"), StringConvert(materialPath.resolvedText()));
        return false;
    }
    if(!hasPixelShader && !material.transparent()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: opaque material '{}' is missing required pixel shader"), StringConvert(materialPath.resolvedText()));
        return false;
    }

    // Material::loadBinary already validated the typed layout (hash, blocks, fields, bytes).
    NWB_ASSERT(material.typedLayoutHash() != 0u && !material.typedBlockBytes().empty());
    NWB_ASSERT(material.typedLayoutBlocks().size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(material.typedLayoutFields().size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(material.typedBlockBytes().size() <= static_cast<usize>(Limit<u32>::s_Max));
    createdInfo.materialInterface = material.materialInterface();

    createdInfo.typedLayoutHash = material.typedLayoutHash();
    createdInfo.typedLayoutBlocks.reserve(material.typedLayoutBlocks().size());
    createdInfo.typedLayoutBlocks.assign(material.typedLayoutBlocks().begin(), material.typedLayoutBlocks().end());
    createdInfo.typedLayoutFields.reserve(material.typedLayoutFields().size());
    createdInfo.typedLayoutFields.assign(material.typedLayoutFields().begin(), material.typedLayoutFields().end());
    createdInfo.resourceReferences.reserve(material.resourceReferences().size());
    createdInfo.resourceReferences.assign(material.resourceReferences().begin(), material.resourceReferences().end());
    SplitMaterialTypedBytesByClass(material, createdInfo.constantTypedBytes, createdInfo.mutableDefaultTypedBytes);
    createdInfo.unpatchedConstantTypedBytes = createdInfo.constantTypedBytes;
    if(!resolveMaterialResourceReferences(createdInfo))
        return false;
    createdInfo.shadingModelId = material.shadingModelId();
    createdInfo.surfaceDispatchId = material.surfaceDispatchId();
    // UINT_MAX marks explicit opaque shaders without a surface hook; keep them out of CSG clipping.
    createdInfo.csgCapSurfaceDispatchAvailable = createdInfo.surfaceDispatchId != Limit<u32>::s_Max;
    createdInfo.transparent = material.transparent();
    createdInfo.twoSided = material.twoSided();
    createdInfo.refractive = material.refractive();

    auto result = m_materialState.m_surfaceInfos.try_emplace(materialPath, Move(createdInfo));
    auto it = result.first;
    outInfo = &it.value();
    NWB_ASSERT(outInfo);
    return true;
}

bool RendererMaterialSystem::findMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset, MaterialSurfaceInfo*& outInfo){
    outInfo = nullptr;

    const Name materialPath = materialAsset.name();
    if(!materialPath)
        return false;

    const auto foundInfo = m_materialState.m_surfaceInfos.find(materialPath);
    if(foundInfo == m_materialState.m_surfaceInfos.end())
        return false;

    MaterialSurfaceInfo& materialInfo = foundInfo.value();
    if(!materialInfo.resourceReferencesResolved)
        return false;

    outInfo = &materialInfo;
    return true;
}

bool RendererMaterialSystem::appendPreparedMaterialSurfaceSampledTextures(
    const MaterialSurfaceInfo& materialInfo,
    MaterialSampledTextureCollector<Core::Alloc::ScratchArena>& collector
){
    return AppendPreparedMaterialSurfaceSampledTextures(materialInfo, m_materialState.m_resourceState, collector);
}

bool RendererMaterialSystem::gatherPreparedMaterialPassSampledTextures(
    const MaterialPassDrawItems* const* const drawItemSets,
    const usize drawItemSetCount,
    Vector<Core::TextureHandle, Core::Alloc::ScratchArena>& outTextures,
    Core::Alloc::ScratchArena& scratchArena
){
    return GatherPreparedMaterialPassSampledTextures(m_materialState.m_surfaceInfos, m_materialState.m_resourceState, drawItemSets, drawItemSetCount, outTextures, scratchArena);
}

bool RendererMaterialSystem::prepareVisibleMaterialSurfaceInfos(){
    bool hasTransparentRenderers = false;
    auto rendererView = m_world.view<RendererComponent>();
    for(auto&& [entity, renderer] : rendererView){
        static_cast<void>(entity);
        if(!renderer.visible)
            continue;

        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!createMaterialSurfaceInfo(renderer.material, materialInfo))
            continue;
        if(materialInfo->transparent)
            hasTransparentRenderers = true;
    }

    return hasTransparentRenderers;
}

void RendererMaterialSystem::prepareVisibleMaterialInstanceMutableCache(){
    pruneMaterialInstanceMutableCache();

    auto rendererView = m_world.view<RendererComponent>();
    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        const MaterialInstanceComponent* materialInstance = m_world.tryGetComponent<MaterialInstanceComponent>(entity);
        if(!materialInstance || materialInstance->overrides.empty())
            continue;

        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!findMaterialSurfaceInfo(renderer.material, materialInfo))
            continue;

        const MaterialTypedByteVector* mutableTypedBytes = nullptr;
        if(!prepareMaterialInstanceMutableTypedBytes(
            entity,
            *materialInfo,
            materialInstance,
            mutableTypedBytes
        ))
            continue;
    }
}

bool RendererMaterialSystem::hasTransparentRenderers(const RendererResourceLookupMode::Enum lookupMode){
    auto materialIsTransparent = [&](const Core::Assets::AssetRef<Material>& material) -> bool{
        MaterialSurfaceInfo* materialInfo = nullptr;
        const bool materialInfoReady = lookupMode == RendererResourceLookupMode::CreateMissing
            ? createMaterialSurfaceInfo(material, materialInfo)
            : findMaterialSurfaceInfo(material, materialInfo)
        ;
        if(!materialInfoReady)
            return false;
        return materialInfo->transparent;
    };

    auto rendererView = m_world.view<RendererComponent>();
    for(auto&& [entity, renderer] : rendererView){
        static_cast<void>(entity);
        if(!renderer.visible)
            continue;

        if(materialIsTransparent(renderer.material))
            return true;
    }
    return false;
}


void RendererMaterialSystem::releaseMaterialResourceReferences(){
    __hidden_material_surface::ReleaseMaterialResourceState(m_graphics, m_materialState.m_resourceState);
    for(auto it = m_materialState.m_surfaceInfos.begin(); it != m_materialState.m_surfaceInfos.end(); ++it){
        MaterialSurfaceInfo& materialInfo = it.value();
        materialInfo.constantTypedBytes = materialInfo.unpatchedConstantTypedBytes;
        materialInfo.resourceReferencesResolved = false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

