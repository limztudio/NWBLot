// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/renderer_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_surface{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_BuiltinCheckerWidth = 2u;
static constexpr u32 s_BuiltinCheckerHeight = 2u;
static constexpr u8 s_CheckerRgba8Pixels[] = {
    255u, 255u, 255u, 255u,  32u,  32u,  32u, 255u,
     32u,  32u,  32u, 255u, 255u, 255u, 255u, 255u,
};
static_assert(sizeof(s_CheckerRgba8Pixels) == s_BuiltinCheckerWidth * s_BuiltinCheckerHeight * sizeof(u32));

static void ReleaseBuiltinChecker(Core::Graphics& graphics, RendererMaterialResourceState& resources){
    Core::GpuDescriptorHeap& heap = graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized() && resources.checkerRgba8HeapHandle.valid())
        heap.free(resources.checkerRgba8HeapHandle);

    resources.checkerRgba8HeapHandle = Core::GpuDescriptorHandle::invalid();
    resources.checkerRgba8Texture.reset();
}

static void ReleaseBuiltinLinearClampSampler(Core::Graphics& graphics, RendererMaterialResourceState& resources){
    Core::GpuDescriptorHeap& heap = graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized() && resources.linearClampHeapHandle.valid())
        heap.free(resources.linearClampHeapHandle);

    resources.linearClampHeapHandle = Core::GpuDescriptorHandle::invalid();
    resources.linearClampSampler.reset();
}

static void ReleaseTextureAssetCache(Core::Graphics& graphics, RendererMaterialResourceState& resources){
    for(auto it = resources.textureAssetCache.begin(); it != resources.textureAssetCache.end(); ++it){
        if(it.value())
            TextureAssetLoader::Release(*it.value(), graphics);
    }
    resources.textureAssetCache.clear();
}

static void ReleaseMaterialResourceState(Core::Graphics& graphics, RendererMaterialResourceState& resources){
    ReleaseTextureAssetCache(graphics, resources);
    ReleaseBuiltinChecker(graphics, resources);
    ReleaseBuiltinLinearClampSampler(graphics, resources);
}

[[nodiscard]] static bool EnsureBuiltinChecker(Core::Graphics& graphics, RendererMaterialResourceState& resources){
    if(resources.checkerRgba8Texture && resources.checkerRgba8HeapHandle.valid())
        return true;
    if(resources.checkerRgba8Texture || resources.checkerRgba8HeapHandle.valid())
        ReleaseBuiltinChecker(graphics, resources);

    Core::GpuDescriptorHeap& heap = graphics.getDevice().getDescriptorHeap();
    Core::TextureDesc textureDesc;
    textureDesc
        .setWidth(s_BuiltinCheckerWidth)
        .setHeight(s_BuiltinCheckerHeight)
        .setFormat(Core::Format::RGBA8_UNORM)
        .setInitialState(Core::ResourceStates::ShaderResource)
        .setKeepInitialState(true)
        // Material surface hooks can run in the optional AsyncCompute trace/GI packets as well as Graphics.
        // The built-in texture is immutable after its Graphics upload, so concurrent sharing avoids a permanent
        // ownership handoff for this common sampled input.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName(Name(MaterialBuiltinResource::s_CheckerRgba8))
    ;
    Core::Graphics::TextureSetupDesc textureSetup;
    textureSetup.textureDesc = textureDesc;
    textureSetup.data = s_CheckerRgba8Pixels;
    textureSetup.uploadDataSize = sizeof(s_CheckerRgba8Pixels);
    textureSetup.rowPitch = s_BuiltinCheckerWidth * sizeof(u32);
    textureSetup.depthPitch = sizeof(s_CheckerRgba8Pixels);
    textureSetup.queue = Core::CommandQueue::Graphics;
    resources.checkerRgba8Texture = graphics.setupTexture(textureSetup);
    if(!resources.checkerRgba8Texture){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create built-in material checker texture"));
        return false;
    }

    resources.checkerRgba8HeapHandle = heap.allocate(Core::GpuDescriptorClass::SampledImage);
    if(
        !resources.checkerRgba8HeapHandle.valid()
        || !heap.write(resources.checkerRgba8HeapHandle, Core::DescriptorWriteItem::Texture_SRV(
            0u,
            resources.checkerRgba8Texture.get(),
            Core::Format::RGBA8_UNORM,
            Core::s_AllSubresources,
            Core::TextureDimension::Texture2D
        ))
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register built-in material checker texture"));
        ReleaseBuiltinChecker(graphics, resources);
        return false;
    }

    return true;
}

[[nodiscard]] static bool EnsureBuiltinLinearClampSampler(Core::Graphics& graphics, RendererMaterialResourceState& resources){
    if(resources.linearClampSampler && resources.linearClampHeapHandle.valid())
        return true;
    if(resources.linearClampSampler || resources.linearClampHeapHandle.valid())
        ReleaseBuiltinLinearClampSampler(graphics, resources);

    Core::GpuDescriptorHeap& heap = graphics.getDevice().getDescriptorHeap();
    Core::SamplerDesc samplerDesc;
    samplerDesc.setAllFilters(true).setAllAddressModes(Core::SamplerAddressMode::Clamp);
    resources.linearClampSampler = graphics.getDevice().createSampler(samplerDesc);
    if(!resources.linearClampSampler){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create built-in material clamp sampler"));
        return false;
    }

    resources.linearClampHeapHandle = heap.allocate(Core::GpuDescriptorClass::Sampler);
    if(
        !resources.linearClampHeapHandle.valid()
        || !heap.write(resources.linearClampHeapHandle, Core::DescriptorWriteItem::Sampler(0u, resources.linearClampSampler.get()))
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register built-in material clamp sampler"));
        ReleaseBuiltinLinearClampSampler(graphics, resources);
        return false;
    }

    return true;
}

[[nodiscard]] static bool ResolveTextureAssetSlot(
    RendererMaterialResourceState& resources,
    const Name& texturePath,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    u32& outHeapSlot
){
    outHeapSlot = 0u;
    auto textureAssetIt = resources.textureAssetCache.find(texturePath);
    if(textureAssetIt == resources.textureAssetCache.end()){
        UniquePtr<TextureGpuResource> textureResource = MakeUnique<TextureGpuResource>();
        if(!TextureAssetLoader::Load(
            *textureResource,
            texturePath,
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
        switch(resourceReference.resourceKind){
        case MaterialResourceKind::SampledImage2D:
            switch(resourceReference.resourceSource){
            case MaterialResourceSource::Builtin:
                if(resourceReference.resourceName != Name(MaterialBuiltinResource::s_CheckerRgba8)){
                    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' requests an unsupported built-in sampled image")
                        , StringConvert(materialInfo.materialName.c_str())
                    );
                    return false;
                }
                if(!__hidden_material_surface::EnsureBuiltinChecker(graphicsModule, resources))
                    return false;
                heapSlot = resources.checkerRgba8HeapHandle.slot();
                break;

            case MaterialResourceSource::TextureAsset:
                if(!__hidden_material_surface::ResolveTextureAssetSlot(
                    resources,
                    resourceReference.resourceName,
                    graphicsModule,
                    assetManager(),
                    heapSlot
                )){
                    NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' failed to load Texture2D asset '{}'")
                        , StringConvert(materialInfo.materialName.c_str())
                        , StringConvert(resourceReference.resourceName.c_str())
                    );
                    return false;
                }
                break;

            default:
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' has an invalid sampled-image resource source")
                    , StringConvert(materialInfo.materialName.c_str())
                );
                return false;
            }
            break;
        case MaterialResourceKind::Sampler:
            if(
                resourceReference.resourceSource != MaterialResourceSource::Builtin
                || resourceReference.resourceName != Name(MaterialBuiltinResource::s_LinearClamp)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' requests an unsupported sampler resource")
                    , StringConvert(materialInfo.materialName.c_str())
                );
                return false;
            }
            if(!__hidden_material_surface::EnsureBuiltinLinearClampSampler(graphicsModule, resources))
                return false;
            heapSlot = resources.linearClampHeapHandle.slot();
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

