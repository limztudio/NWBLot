// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"
#include "sampled_texture_collection.h"

#include <impl/assets_material/asset.h>
#include <impl/assets_shader/asset.h>
#include <impl/ecs_render/material/renderer_material_state.h>
#include <impl/assets_sampler/loader.h>
#include <impl/assets_texture/loader.h>
#include <impl/ecs_render/shader/shader_system.h>

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>


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
    ReleaseAssetCache(resources.textureAssetCache, [&](TextureGpuResource& resource){ TextureAssetLoader::Release(resource, graphics); });
}

static void ReleaseSamplerAssetCache(Core::GraphicsRuntime& graphics, RendererMaterialResourceState& resources){
    ReleaseAssetCache(resources.samplerAssetCache, [&](SamplerGpuResource& resource){ SamplerAssetLoader::Release(resource, graphics); });
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

[[nodiscard]] static b8 WriteMaterialInstanceOverrideBytes(
    const Core::ECS::EntityID entity,
    const Name& materialName,
    const MaterialInstanceParameter& parameter,
    const MaterialTypedLayoutField& field,
    const u32 byteOffset,
    const Span<u8> inOutMutableTypedBytes
){
    const u32 fieldByteSize = MaterialLayoutFieldByteSize(field.fieldType);
    NWB_ASSERT(fieldByteSize != 0u);
    if(byteOffset > inOutMutableTypedBytes.size() || static_cast<usize>(fieldByteSize) > inOutMutableTypedBytes.size() - static_cast<usize>(byteOffset)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override '{}' for entity {} exceeds mutable storage for material '{}'"), ConvertString(parameter.parameterName.resolvedText()), entity.id, ConvertString(materialName.resolvedText()));
        return false;
    }

    const NotNull<const u8*> valueBytes = MakeNotNull(static_cast<const u8*>(static_cast<const void*>(parameter.value.raw)));
    NWB_MEMCPY(inOutMutableTypedBytes.data() + byteOffset, fieldByteSize, valueBytes.get(), fieldByteSize);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialInstanceOverrideField> RendererMaterialSystem::FindMaterialInstanceOverrideField(const Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo, const MaterialInstanceParameter& parameter){
    MaterialInstanceOverrideField resolvedField;
    if(TryFindMaterialInstanceOverrideField(materialInfo.typedLayoutBlocks, materialInfo.typedLayoutFields, parameter, resolvedField))
        return resolvedField;

    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override '{}' for entity {} is not declared by material '{}'"), ConvertString(parameter.parameterName.resolvedText()), entity.id, ConvertString(materialInfo.materialName.resolvedText()));
    return MakeUnexpected(Failure{});
}

b8 RendererMaterialSystem::ApplyMaterialInstanceOverrides(const Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo, const MaterialInstanceComponent& materialInstance, const Span<u8> inOutMutableTypedBytes){
    if(!materialInstance.materialInterface){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance overrides for entity {} require a material interface"), entity.id);
        return false;
    }
    if(materialInstance.materialInterface != materialInfo.materialInterface){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance overrides for entity {} target interface '{}' but material '{}' uses '{}'")
            , entity.id
            , ConvertString(materialInstance.materialInterface.resolvedText())
            , ConvertString(materialInfo.materialName.resolvedText())
            , ConvertString(materialInfo.materialInterface.resolvedText())
        );
        return false;
    }

    for(const MaterialInstanceParameter& parameter : materialInstance.overrides){
        if(!parameter.blockName || !parameter.fieldName){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override for entity {} has an invalid parameter name"), entity.id);
            return false;
        }

        const auto resolvedField = FindMaterialInstanceOverrideField(entity, materialInfo, parameter);
        if(!resolvedField)
            return false;

        const MaterialTypedLayoutField& field = resolvedField->field;
        if(!resolvedField->mutableBlock){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override '{}' for entity {} targets material-constant storage"), ConvertString(parameter.parameterName.resolvedText()), entity.id);
            return false;
        }
        if(field.fieldType != parameter.fieldType){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material instance override '{}' for entity {} type does not match material '{}'")
                , ConvertString(parameter.parameterName.resolvedText())
                , entity.id
                , ConvertString(materialInfo.materialName.resolvedText())
            );
            return false;
        }

        NWB_ASSERT(field.offset <= Limit<u32>::s_Max - resolvedField->blockByteBegin);
        const u32 fieldByteOffset = resolvedField->blockByteBegin + field.offset;
        if(!__hidden_material_surface::WriteMaterialInstanceOverrideBytes(entity, materialInfo.materialName, parameter, field, fieldByteOffset, inOutMutableTypedBytes))
            return false;
    }

    return true;
}

void RendererMaterialSystem::SplitMaterialTypedBytesByClass(const Material& material, MaterialTypedByteVector& outConstantTypedBytes, MaterialTypedByteVector& outMutableDefaultTypedBytes){
    NWB_ASSERT(material.typedLayoutHash() != 0u);
    outConstantTypedBytes.clear();
    outMutableDefaultTypedBytes.clear();

    const auto& packedTypedBytes = material.typedBlockBytes();
    usize constantByteSize = 0u;
    usize mutableByteSize = 0u;
    for(const MaterialTypedLayoutBlock& block : material.typedLayoutBlocks()){
        usize& targetByteSize = block.blockClass == MaterialBlockClass::MaterialConstant ? constantByteSize : mutableByteSize;
        targetByteSize += block.byteSize;
    }
    outConstantTypedBytes.reserve(constantByteSize);
    outMutableDefaultTypedBytes.reserve(mutableByteSize);
    usize sourceByteOffset = 0u;
    for(const MaterialTypedLayoutBlock& block : material.typedLayoutBlocks()){
        // Material::loadBinary already ran ValidateMaterialTypedLayout; keep a debug-only invariant here.
        NWB_ASSERT(IsValidMaterialBlockClass(block.blockClass));
        NWB_ASSERT((block.byteSize & (sizeof(u32) - 1u)) == 0u);
        NWB_ASSERT(sourceByteOffset <= packedTypedBytes.size() && block.byteSize <= packedTypedBytes.size() - sourceByteOffset);

        MaterialTypedByteVector& targetTypedBytes = block.blockClass == MaterialBlockClass::MaterialConstant ? outConstantTypedBytes : outMutableDefaultTypedBytes;
        targetTypedBytes.insert(targetTypedBytes.end(), packedTypedBytes.begin() + sourceByteOffset, packedTypedBytes.begin() + sourceByteOffset + block.byteSize);
        sourceByteOffset += block.byteSize;
    }
    // Material::loadBinary already validated the packed byte count against the cooked layout.
    NWB_ASSERT(sourceByteOffset == packedTypedBytes.size());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<NotNull<MaterialSurfaceInfo*>> RendererMaterialSystem::createMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset){
    const Name materialPath = materialAsset.name();
    if(!materialPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer material is empty"));
        return MakeUnexpected(Failure{});
    }

    const auto foundInfo = m_materialState.m_surfaceInfos.find(materialPath);
    if(foundInfo != m_materialState.m_surfaceInfos.end()){
        MaterialSurfaceInfo& info = foundInfo.value();
        if(!resolveMaterialResourceReferences(info))
            return MakeUnexpected(Failure{});
        return MakeNotNull(&info);
    }

    auto loadedAsset = m_assetManager.loadTypedSync<Material>(materialPath, s_RendererAssetOwnerName);
    if(!loadedAsset)
        return MakeUnexpected(Failure{});

    const Material& material = **loadedAsset;

    MaterialSurfaceInfo createdInfo(m_arena);
    createdInfo.materialName = materialPath;
    // Material::loadBinary already rejected empty shader variants and missing material interfaces.
    NWB_ASSERT(!material.shaderVariant().empty());
    NWB_ASSERT(material.materialInterface());
    createdInfo.shaderVariant.reserve(material.shaderVariant().size());
    createdInfo.shaderVariant.assign(material.shaderVariant().data(), material.shaderVariant().size());

    const auto pixelShader = material.findShader<PixelShader>();
    const auto meshShader = material.findShader<MeshShader>();
    const b8 hasPixelShader = pixelShader.has_value();
    const b8 hasMeshShader = meshShader.has_value();
    if(pixelShader)
        createdInfo.pixelShader = *pixelShader;
    if(meshShader)
        createdInfo.meshShader = *meshShader;
    createdInfo.avboitAccumulatePixelShader = material.avboitAccumulatePixelShader();
    createdInfo.avboitOccupancyPixelShader = material.avboitOccupancyPixelShader();
    createdInfo.avboitExtinctionPixelShader = material.avboitExtinctionPixelShader();
    if(!hasMeshShader){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' is missing required mesh shader"), ConvertString(materialPath.resolvedText()));
        return MakeUnexpected(Failure{});
    }
    if(!hasPixelShader && !material.transparent()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: opaque material '{}' is missing required pixel shader"), ConvertString(materialPath.resolvedText()));
        return MakeUnexpected(Failure{});
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
        return MakeUnexpected(Failure{});
    createdInfo.shadingModelId = material.shadingModelId();
    createdInfo.surfaceDispatchId = material.surfaceDispatchId();
    // UINT_MAX marks explicit opaque shaders without a surface hook; keep them out of CSG clipping.
    createdInfo.csgCapSurfaceDispatchAvailable = createdInfo.surfaceDispatchId != Limit<u32>::s_Max;
    createdInfo.transparent = material.transparent();
    createdInfo.twoSided = material.twoSided();
    createdInfo.refractive = material.refractive();

    auto result = m_materialState.m_surfaceInfos.try_emplace(materialPath, Move(createdInfo));
    auto it = result.first;
    return MakeNotNull(&it.value());
}

Expected<NotNull<MaterialSurfaceInfo*>> RendererMaterialSystem::findMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset)noexcept{
    const Name materialPath = materialAsset.name();
    if(!materialPath)
        return MakeUnexpected(Failure{});

    const auto foundInfo = m_materialState.m_surfaceInfos.find(materialPath);
    if(foundInfo == m_materialState.m_surfaceInfos.end())
        return MakeUnexpected(Failure{});

    MaterialSurfaceInfo& materialInfo = foundInfo.value();
    if(!materialInfo.resourceReferencesResolved)
        return MakeUnexpected(Failure{});

    return MakeNotNull(&materialInfo);
}

b8 RendererMaterialSystem::resolveMaterialResourceReferences(MaterialSurfaceInfo& materialInfo){
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
        Expected<u32> heapSlot = MakeUnexpected(Failure{});
        switch(resourceReference.resourceKind){
        case MaterialResourceKind::SampledImage2D:
            heapSlot = __hidden_material_surface::ResolveTextureAssetSlot(resources, resourceReference.textureAsset, graphicsModule, m_assetManager);
            if(!heapSlot){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' failed to load Texture2D asset '{}'"), ConvertString(materialInfo.materialName.resolvedText()), ConvertString(resourceReference.textureAsset.name().resolvedText()));
                return false;
            }
            break;
        case MaterialResourceKind::Sampler:
            heapSlot = __hidden_material_surface::ResolveSamplerAssetSlot(resources, resourceReference.samplerAsset, graphicsModule, m_assetManager);
            if(!heapSlot){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' failed to load sampler asset '{}'"), ConvertString(materialInfo.materialName.resolvedText()), ConvertString(resourceReference.samplerAsset.name().resolvedText()));
                return false;
            }
            break;
        default:
            NWB_ASSERT(false);
            return false;
        }

        const u32 resolvedHeapSlot = *heapSlot;
        NWB_ASSERT(resourceReference.constantByteOffset <= materialInfo.constantTypedBytes.size() && sizeof(resolvedHeapSlot) <= materialInfo.constantTypedBytes.size() - resourceReference.constantByteOffset);
        NWB_MEMCPY(materialInfo.constantTypedBytes.data() + resourceReference.constantByteOffset, materialInfo.constantTypedBytes.size() - resourceReference.constantByteOffset, &resolvedHeapSlot, sizeof(resolvedHeapSlot));
    }

    materialInfo.resourceReferencesResolved = true;
    return true;
}

b8 RendererMaterialSystem::prepareVisibleMaterialSurfaceInfos(){
    b8 hasTransparentRenderers = false;
    auto rendererView = m_world.view<RendererComponent>();
    for([[maybe_unused]] auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        const auto materialInfoResult = createMaterialSurfaceInfo(renderer.material);
        if(!materialInfoResult)
            continue;
        const NotNull<MaterialSurfaceInfo*> materialInfo = *materialInfoResult;
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

        const auto materialInfoResult = findMaterialSurfaceInfo(renderer.material);
        if(!materialInfoResult)
            continue;
        const NotNull<MaterialSurfaceInfo*> materialInfo = *materialInfoResult;

        if(!prepareMaterialInstanceMutableTypedBytes(entity, *materialInfo, materialInstance))
            continue;
    }
}

b8 RendererMaterialSystem::hasTransparentRenderers(const RendererResourceLookupMode::Enum lookupMode){
    auto materialIsTransparent = [&](const Core::Assets::AssetRef<Material>& material) -> b8{
        const auto materialInfoResult = lookupMode == RendererResourceLookupMode::CreateMissing ? createMaterialSurfaceInfo(material) : findMaterialSurfaceInfo(material);
        if(!materialInfoResult)
            return false;
        const NotNull<MaterialSurfaceInfo*> materialInfo = *materialInfoResult;
        return materialInfo->transparent;
    };

    auto rendererView = m_world.view<RendererComponent>();
    for([[maybe_unused]] auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        if(materialIsTransparent(renderer.material))
            return true;
    }
    return false;
}

Expected<Span<const u8>> RendererMaterialSystem::prepareMaterialInstanceMutableTypedBytes(
    const Core::ECS::EntityID entity,
    const MaterialSurfaceInfo& materialInfo,
    const MaterialInstanceComponent* materialInstance
){
    pruneMaterialInstanceMutableCache();

    if(!materialInstance || materialInstance->overrides.empty())
        return Span<const u8>(materialInfo.mutableDefaultTypedBytes);

    auto it = m_materialState.m_instanceMutableCache.try_emplace(entity, m_arena).first;
    MaterialInstanceMutableCacheEntry& cacheEntry = it.value();
    if(::NWB::Impl::MaterialInstanceMutableCacheEntryMatches(
        cacheEntry.materialName,
        cacheEntry.materialInterface,
        cacheEntry.typedLayoutHash,
        cacheEntry.revision,
        materialInfo.materialName,
        materialInfo.materialInterface,
        materialInfo.typedLayoutHash,
        *materialInstance
    ))
        return Span<const u8>(cacheEntry.mutableTypedBytes);

    AssignTriviallyCopyableVector(cacheEntry.mutableTypedBytes, materialInfo.mutableDefaultTypedBytes);
    if(!ApplyMaterialInstanceOverrides(entity, materialInfo, *materialInstance, cacheEntry.mutableTypedBytes)){
        m_materialState.m_instanceMutableCache.erase(it);
        return MakeUnexpected(Failure{});
    }

    cacheEntry.materialName = materialInfo.materialName;
    cacheEntry.materialInterface = materialInfo.materialInterface;
    cacheEntry.typedLayoutHash = materialInfo.typedLayoutHash;
    cacheEntry.revision = materialInstance->revision;

    return Span<const u8>(cacheEntry.mutableTypedBytes);
}

Expected<Span<const u8>> RendererMaterialSystem::findPreparedMaterialInstanceMutableTypedBytes(const Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo, const MaterialInstanceComponent* materialInstance)const noexcept{
    if(!materialInstance || materialInstance->overrides.empty())
        return Span<const u8>(materialInfo.mutableDefaultTypedBytes);

    const auto found = m_materialState.m_instanceMutableCache.find(entity);
    if(found == m_materialState.m_instanceMutableCache.end())
        return MakeUnexpected(Failure{});

    const MaterialInstanceMutableCacheEntry& cacheEntry = found.value();
    if(!::NWB::Impl::MaterialInstanceMutableCacheEntryMatches(
        cacheEntry.materialName,
        cacheEntry.materialInterface,
        cacheEntry.typedLayoutHash,
        cacheEntry.revision,
        materialInfo.materialName,
        materialInfo.materialInterface,
        materialInfo.typedLayoutHash,
        *materialInstance
    ))
        return MakeUnexpected(Failure{});

    return Span<const u8>(cacheEntry.mutableTypedBytes);
}

Expected<ShadowOccluderMaterialContext> RendererMaterialSystem::appendShadowOccluderMaterialContext(
    const Core::ECS::EntityID entity,
    const MaterialSurfaceInfo& materialInfo,
    const NWB::Impl::Scene::TransformComponent* transform,
    MaterialTypedByteDataVector& inOutMaterialTypedBytes,
    ECSRenderDetail::MaterialTypedConstantRangeMap& inOutConstantRanges,
    ECSRenderDetail::MaterialTypedByteContentRangeMap& inOutMutableRanges
){
    const auto constantRange = ECSRenderDetail::FindOrAppendMaterialConstantTypedByteRange(materialInfo.materialName, materialInfo.typedLayoutHash, materialInfo.constantTypedBytes, inOutMaterialTypedBytes, inOutConstantRanges);
    if(!constantRange)
        return MakeUnexpected(Failure{});

    const MaterialInstanceComponent* materialInstance = m_world.tryGetComponent<MaterialInstanceComponent>(entity);
    const auto mutableTypedBytes = prepareMaterialInstanceMutableTypedBytes(entity, materialInfo, materialInstance);
    if(!mutableTypedBytes)
        return MakeUnexpected(Failure{});
    const auto mutableRange = ECSRenderDetail::FindOrAppendMaterialTypedByteRange(*mutableTypedBytes, inOutMaterialTypedBytes, inOutMutableRanges);
    if(!mutableRange)
        return MakeUnexpected(Failure{});

    NWB_ASSERT(MaterialTypedByteRangeEmptyOffsetValid(*constantRange));
    NWB_ASSERT(MaterialTypedByteRangeEmptyOffsetValid(*mutableRange));
    const NWB::Impl::Scene::TransformComponent defaultTransform;
    const NWB::Impl::Scene::TransformComponent& resolvedTransform = transform ? *transform : defaultTransform;
    return ShadowOccluderMaterialContext{
        ::NWB::Impl::BuildInstanceGpuData(entity, transform != nullptr, resolvedTransform.position, resolvedTransform.rotation, resolvedTransform.scale, mutableRange->byteOffset),
        constantRange->byteOffset
    };
}

void RendererMaterialSystem::pruneMaterialInstanceMutableCache()noexcept{
    const u64 componentMutationVersion = m_world.componentMutationVersion<MaterialInstanceComponent>();
    if(componentMutationVersion == m_materialState.m_instanceMutableCacheComponentMutationVersion)
        return;

    m_materialState.m_instanceMutableCache.clear();
    m_materialState.m_instanceMutableCacheComponentMutationVersion = componentMutationVersion;
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

