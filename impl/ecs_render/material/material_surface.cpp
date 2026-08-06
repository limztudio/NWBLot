// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/renderer_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_surface{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ReleaseTextureAssetCache(Core::Graphics& graphics, RendererMaterialResourceState& resources){
    for(auto it = resources.textureAssetCache.begin(); it != resources.textureAssetCache.end(); ++it){
        if(it.value())
            TextureAssetLoader::Release(*it.value(), graphics);
    }
    resources.textureAssetCache.clear();
}

static void ReleaseSamplerAssetCache(Core::Graphics& graphics, RendererMaterialResourceState& resources){
    for(auto it = resources.samplerAssetCache.begin(); it != resources.samplerAssetCache.end(); ++it){
        if(it.value())
            SamplerAssetLoader::Release(*it.value(), graphics);
    }
    resources.samplerAssetCache.clear();
}

static void ReleaseMaterialResourceState(Core::Graphics& graphics, RendererMaterialResourceState& resources){
    ReleaseTextureAssetCache(graphics, resources);
    ReleaseSamplerAssetCache(graphics, resources);
}

[[nodiscard]] static bool ResolveTextureAssetSlot(
    RendererMaterialResourceState& resources,
    const Core::Assets::AssetRef<Texture>& textureAsset,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    u32& outHeapSlot
){
    outHeapSlot = 0u;
    if(!textureAsset.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material Texture2D asset reference is empty"));
        return false;
    }

    const Name& texturePath = textureAsset.name();
    auto textureAssetIt = resources.textureAssetCache.find(texturePath);
    if(textureAssetIt == resources.textureAssetCache.end()){
        UniquePtr<TextureGpuResource> textureResource = MakeUnique<TextureGpuResource>();
        if(!TextureAssetLoader::Load(
            *textureResource,
            textureAsset,
            texturePath,
            graphics,
            assetManager,
            NWB_TEXT("RendererSystem")
        ))
            return false;

        if(textureResource->sampledImageHeapHandle.descriptorClass() != Core::GpuDescriptorClass::SampledImage){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: Texture2D asset '{}' has an incompatible texture dimension")
                , StringConvert(texturePath.c_str())
            );
            TextureAssetLoader::Release(*textureResource, graphics);
            return false;
        }

        auto insertResult = resources.textureAssetCache.try_emplace(texturePath, Move(textureResource));
        textureAssetIt = insertResult.first;
        if(!insertResult.second && textureResource)
            TextureAssetLoader::Release(*textureResource, graphics);
    }

    NWB_ASSERT(textureAssetIt.value());
    const TextureGpuResource& textureResource = *textureAssetIt.value();
    if(!textureResource.valid() || textureResource.sampledImageHeapHandle.descriptorClass() != Core::GpuDescriptorClass::SampledImage){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cached Texture2D asset '{}' is invalid")
            , StringConvert(texturePath.c_str())
        );
        return false;
    }

    outHeapSlot = textureResource.sampledImageHeapHandle.slot();
    return true;
}

[[nodiscard]] static bool ResolveSamplerAssetSlot(
    RendererMaterialResourceState& resources,
    const Core::Assets::AssetRef<Sampler>& samplerAsset,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    u32& outHeapSlot
){
    outHeapSlot = 0u;
    if(!samplerAsset.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material sampler asset reference is empty"));
        return false;
    }

    const Name& samplerPath = samplerAsset.name();
    auto samplerAssetIt = resources.samplerAssetCache.find(samplerPath);
    if(samplerAssetIt == resources.samplerAssetCache.end()){
        UniquePtr<SamplerGpuResource> samplerResource = MakeUnique<SamplerGpuResource>();
        if(!SamplerAssetLoader::Load(
            *samplerResource,
            samplerAsset,
            samplerPath,
            graphics,
            assetManager,
            NWB_TEXT("RendererSystem")
        ))
            return false;

        auto insertResult = resources.samplerAssetCache.try_emplace(samplerPath, Move(samplerResource));
        samplerAssetIt = insertResult.first;
        if(!insertResult.second && samplerResource)
            SamplerAssetLoader::Release(*samplerResource, graphics);
    }

    NWB_ASSERT(samplerAssetIt.value());
    const SamplerGpuResource& samplerResource = *samplerAssetIt.value();
    if(
        !samplerResource.valid()
        || samplerResource.samplerHeapHandle.descriptorClass() != Core::GpuDescriptorClass::Sampler
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cached sampler asset '{}' is invalid")
            , StringConvert(samplerPath.c_str())
        );
        return false;
    }

    outHeapSlot = samplerResource.samplerHeapHandle.slot();
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

    RendererMaterialResourceState& resources = materialState().m_resourceState;
    Core::Graphics& graphicsModule = graphics();
    Core::GpuDescriptorHeap& heap = graphicsModule.getDevice().getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cannot resolve material resources without an initialized descriptor heap"));
        return false;
    }

    for(const MaterialResourceReference& resourceReference : materialInfo.resourceReferences){
        u32 heapSlot = 0u;
        if(resourceReference.resourceSource != MaterialResourceSource::Asset){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' has an invalid asset resource source")
                , StringConvert(materialInfo.materialName.c_str())
            );
            return false;
        }

        switch(resourceReference.resourceKind){
        case MaterialResourceKind::SampledImage2D:
            if(!__hidden_material_surface::ResolveTextureAssetSlot(
                resources,
                resourceReference.textureAsset,
                graphicsModule,
                assetManager(),
                heapSlot
            )){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' failed to load Texture2D asset '{}'")
                    , StringConvert(materialInfo.materialName.c_str())
                    , StringConvert(resourceReference.textureAsset.name().c_str())
                );
                return false;
            }
            break;
        case MaterialResourceKind::Sampler:
            if(!__hidden_material_surface::ResolveSamplerAssetSlot(
                resources,
                resourceReference.samplerAsset,
                graphicsModule,
                assetManager(),
                heapSlot
            )){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' failed to load sampler asset '{}'")
                    , StringConvert(materialInfo.materialName.c_str())
                    , StringConvert(resourceReference.samplerAsset.name().c_str())
                );
                return false;
            }
            break;
        default:
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' has an invalid material resource kind")
                , StringConvert(materialInfo.materialName.c_str())
            );
            return false;
        }

        if(
            resourceReference.constantByteOffset > materialInfo.constantTypedBytes.size()
            || sizeof(heapSlot) > materialInfo.constantTypedBytes.size() - resourceReference.constantByteOffset
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' resource slot exceeds constant typed bytes")
                , StringConvert(materialInfo.materialName.c_str())
            );
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

void RendererMaterialSystem::releaseMaterialResourceReferences(){
    __hidden_material_surface::ReleaseMaterialResourceState(graphics(), materialState().m_resourceState);
    for(auto it = materialState().m_surfaceInfos.begin(); it != materialState().m_surfaceInfos.end(); ++it){
        MaterialSurfaceInfo& materialInfo = it.value();
        materialInfo.constantTypedBytes = materialInfo.unpatchedConstantTypedBytes;
        materialInfo.resourceReferencesResolved = false;
    }
}

bool RendererMaterialSystem::splitMaterialTypedBytesByClass(
    const Material& material,
    const Name& materialPath,
    MaterialTypedByteVector& outConstantTypedBytes,
    MaterialTypedByteVector& outMutableDefaultTypedBytes
){
    outConstantTypedBytes.clear();
    outMutableDefaultTypedBytes.clear();

    const auto& packedTypedBytes = material.typedBlockBytes();
    usize sourceByteOffset = 0u;
    for(const MaterialTypedLayoutBlock& block : material.typedLayoutBlocks()){
        if(!IsValidMaterialBlockClass(block.blockClass)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' has invalid typed material block class")
                , StringConvert(materialPath.c_str())
            );
            return false;
        }
        if((block.byteSize & (sizeof(u32) - 1u)) != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material block size is not u32 aligned")
                , StringConvert(materialPath.c_str())
            );
            return false;
        }
        if(sourceByteOffset > packedTypedBytes.size() || block.byteSize > packedTypedBytes.size() - sourceByteOffset){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material block bytes exceed packed data")
                , StringConvert(materialPath.c_str())
            );
            return false;
        }

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
    if(sourceByteOffset != packedTypedBytes.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material layout size does not match packed data")
            , StringConvert(materialPath.c_str())
        );
        return false;
    }

    return true;
}

bool RendererMaterialSystem::createMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset, MaterialSurfaceInfo*& outInfo){
    outInfo = nullptr;

    const Name materialPath = materialAsset.name();
    if(!materialPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer material is empty"));
        return false;
    }

    const auto foundInfo = materialState().m_surfaceInfos.find(materialPath);
    if(foundInfo != materialState().m_surfaceInfos.end()){
        outInfo = &foundInfo.value();
        return resolveMaterialResourceReferences(*outInfo);
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!assetManager().loadSync(Material::AssetTypeName(), materialPath, loadedAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to load material '{}'"), StringConvert(materialPath.c_str()));
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Material::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: asset '{}' is not a material"), StringConvert(materialPath.c_str()));
        return false;
    }

    const Material& material = static_cast<const Material&>(*loadedAsset);
    const auto& typedBlockBytes = material.typedBlockBytes();

    MaterialSurfaceInfo createdInfo(arena());
    createdInfo.materialName = materialPath;
    if(material.shaderVariant().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' has empty shader variant")
            , StringConvert(materialPath.c_str())
        );
        return false;
    }
    createdInfo.shaderVariant.assign(material.shaderVariant().data(), material.shaderVariant().size());

    const bool hasPixelShader = material.findShaderForStage(Core::ShaderType::PixelStage, createdInfo.pixelShader);
    const bool hasMeshShader = material.findShaderForStage(Core::ShaderType::MeshStage, createdInfo.meshShader);
    createdInfo.avboitAccumulatePixelShader = material.avboitAccumulatePixelShader();
    createdInfo.avboitOccupancyPixelShader = material.avboitOccupancyPixelShader();
    createdInfo.avboitExtinctionPixelShader = material.avboitExtinctionPixelShader();
    if(!hasMeshShader){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' is missing required mesh shader")
            , StringConvert(materialPath.c_str())
        );
        return false;
    }
    if(!hasPixelShader && !material.transparent()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: opaque material '{}' is missing required pixel shader")
            , StringConvert(materialPath.c_str())
        );
        return false;
    }

    if(!material.materialInterface()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' is missing required material interface")
            , StringConvert(materialPath.c_str())
        );
        return false;
    }
    createdInfo.materialInterface = material.materialInterface();
    if(material.typedLayoutHash() == 0u || typedBlockBytes.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' is missing typed material data")
            , StringConvert(materialPath.c_str())
        );
        return false;
    }
    if(material.typedLayoutBlocks().size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material block count exceeds u32 limits")
            , StringConvert(materialPath.c_str())
        );
        return false;
    }
    if(material.typedLayoutFields().size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material field count exceeds u32 limits")
            , StringConvert(materialPath.c_str())
        );
        return false;
    }
    if(typedBlockBytes.size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material data exceeds u32 limits")
            , StringConvert(materialPath.c_str())
        );
        return false;
    }

    createdInfo.typedLayoutHash = material.typedLayoutHash();
    createdInfo.typedLayoutBlocks.assign(material.typedLayoutBlocks().begin(), material.typedLayoutBlocks().end());
    createdInfo.typedLayoutFields.assign(material.typedLayoutFields().begin(), material.typedLayoutFields().end());
    createdInfo.resourceReferences.assign(material.resourceReferences().begin(), material.resourceReferences().end());
    if(!splitMaterialTypedBytesByClass(
        material,
        materialPath,
        createdInfo.constantTypedBytes,
        createdInfo.mutableDefaultTypedBytes
    ))
        return false;
    createdInfo.unpatchedConstantTypedBytes = createdInfo.constantTypedBytes;
    if(!resolveMaterialResourceReferences(createdInfo))
        return false;
    createdInfo.shadingModelId = material.shadingModelId();
    createdInfo.shadowTransmittanceModelId = material.shadowTransmittanceModelId();
    // The material cook reserves UINT_MAX for explicit opaque stage shaders, which provide no project-owned
    // surface hook. CSG caps must evaluate that hook through the typed material context, so those materials are
    // deliberately kept out of CSG clipping instead of receiving an inferred/fallback cap color.
    createdInfo.csgCapSurfaceDispatchAvailable = createdInfo.shadowTransmittanceModelId != Limit<u32>::s_Max;
    createdInfo.transparent = material.transparent();
    createdInfo.twoSided = material.twoSided();
    createdInfo.refractive = material.refractive();

    auto result = materialState().m_surfaceInfos.try_emplace(materialPath, Move(createdInfo));
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

    const auto foundInfo = materialState().m_surfaceInfos.find(materialPath);
    if(foundInfo == materialState().m_surfaceInfos.end())
        return false;

    MaterialSurfaceInfo& materialInfo = foundInfo.value();
    if(!materialInfo.resourceReferencesResolved)
        return false;

    outInfo = &materialInfo;
    return true;
}

bool RendererMaterialSystem::prepareVisibleMaterialSurfaceInfos(){
    bool hasTransparentRenderers = false;
    auto rendererView = world().view<RendererComponent>();
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

    auto rendererView = world().view<RendererComponent>();
    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        const MaterialInstanceComponent* materialInstance = world().tryGetComponent<MaterialInstanceComponent>(entity);
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

    auto rendererView = world().view<RendererComponent>();
    for(auto&& [entity, renderer] : rendererView){
        static_cast<void>(entity);
        if(!renderer.visible)
            continue;

        if(materialIsTransparent(renderer.material))
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

