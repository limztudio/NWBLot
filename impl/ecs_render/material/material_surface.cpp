// limztudio@gmail.com


#include <impl/ecs_render/kernel/renderer_private.h>


NWB_IMPL_BEGIN


namespace __hidden_material_surface{


static constexpr u32 s_FixtureCheckerWidth = 2u;
static constexpr u32 s_FixtureCheckerHeight = 2u;
static constexpr u8 s_CheckerRgba8Pixels[] = {
    255u, 255u, 255u, 255u,  32u,  32u,  32u, 255u,
     32u,  32u,  32u, 255u, 255u, 255u, 255u, 255u,
};
static_assert(sizeof(s_CheckerRgba8Pixels) == s_FixtureCheckerWidth * s_FixtureCheckerHeight * sizeof(u32));

static void ReleaseFixtureHeapHandles(Core::Graphics& graphics, RendererMaterialResourceFixtureState& fixtures){
    Core::GpuDescriptorHeap& heap = graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized()){
        if(fixtures.checkerRgba8HeapHandle.valid())
            heap.free(fixtures.checkerRgba8HeapHandle);
        if(fixtures.linearClampHeapHandle.valid())
            heap.free(fixtures.linearClampHeapHandle);
    }

    fixtures = RendererMaterialResourceFixtureState{};
}


};


bool RendererMaterialSystem::resolveMaterialResourceFixtures(MaterialSurfaceInfo& materialInfo){
    if(materialInfo.resourceFixturesResolved)
        return true;

    materialInfo.constantTypedBytes = materialInfo.unpatchedConstantTypedBytes;
    if(materialInfo.resourceReferences.empty()){
        materialInfo.resourceFixturesResolved = true;
        return true;
    }

    RendererMaterialResourceFixtureState& fixtures = materialState().m_resourceFixtures;
    Core::Graphics& graphicsModule = graphics();
    Core::GpuDescriptorHeap& heap = graphicsModule.getDevice().getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cannot resolve material resource fixtures without an initialized descriptor heap"));
        return false;
    }

    const bool fixtureCacheReady =
        fixtures.checkerRgba8Texture
        && fixtures.linearClampSampler
        && fixtures.checkerRgba8HeapHandle.valid()
        && fixtures.linearClampHeapHandle.valid()
    ;
    if(!fixtureCacheReady){
        __hidden_material_surface::ReleaseFixtureHeapHandles(graphicsModule, fixtures);

        Core::TextureDesc textureDesc;
        textureDesc
            .setWidth(__hidden_material_surface::s_FixtureCheckerWidth)
            .setHeight(__hidden_material_surface::s_FixtureCheckerHeight)
            .setFormat(Core::Format::RGBA8_UNORM)
            .setInitialState(Core::ResourceStates::ShaderResource)
            .setKeepInitialState(true)
            // Material surface hooks can run in the optional AsyncCompute trace/GI packets as well as Graphics.
            // The fixture is immutable after its Graphics upload, so concurrent sharing avoids a permanent
            // ownership handoff for this common sampled input.
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setName(Name(MaterialResourceFixture::s_CheckerRgba8))
        ;
        Core::Graphics::TextureSetupDesc textureSetup;
        textureSetup.textureDesc = textureDesc;
        textureSetup.data = __hidden_material_surface::s_CheckerRgba8Pixels;
        textureSetup.uploadDataSize = sizeof(__hidden_material_surface::s_CheckerRgba8Pixels);
        textureSetup.rowPitch = __hidden_material_surface::s_FixtureCheckerWidth * sizeof(u32);
        textureSetup.depthPitch = sizeof(__hidden_material_surface::s_CheckerRgba8Pixels);
        textureSetup.queue = Core::CommandQueue::Graphics;
        fixtures.checkerRgba8Texture = graphicsModule.setupTexture(textureSetup);
        if(!fixtures.checkerRgba8Texture){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create material checker texture fixture"));
            return false;
        }

        Core::SamplerDesc samplerDesc;
        samplerDesc.setAllFilters(true).setAllAddressModes(Core::SamplerAddressMode::Clamp);
        fixtures.linearClampSampler = graphicsModule.getDevice().createSampler(samplerDesc);
        if(!fixtures.linearClampSampler){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create material clamp sampler fixture"));
            __hidden_material_surface::ReleaseFixtureHeapHandles(graphicsModule, fixtures);
            return false;
        }

        fixtures.checkerRgba8HeapHandle = heap.allocate(Core::GpuDescriptorClass::SampledImage);
        if(
            !fixtures.checkerRgba8HeapHandle.valid()
            || !heap.write(fixtures.checkerRgba8HeapHandle, Core::DescriptorWriteItem::Texture_SRV(
                0u,
                fixtures.checkerRgba8Texture.get(),
                Core::Format::RGBA8_UNORM,
                Core::s_AllSubresources,
                Core::TextureDimension::Texture2D
            ))
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register material checker texture fixture in the descriptor heap"));
            __hidden_material_surface::ReleaseFixtureHeapHandles(graphicsModule, fixtures);
            return false;
        }

        fixtures.linearClampHeapHandle = heap.allocate(Core::GpuDescriptorClass::Sampler);
        if(
            !fixtures.linearClampHeapHandle.valid()
            || !heap.write(fixtures.linearClampHeapHandle, Core::DescriptorWriteItem::Sampler(0u, fixtures.linearClampSampler.get()))
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register material clamp sampler fixture in the descriptor heap"));
            __hidden_material_surface::ReleaseFixtureHeapHandles(graphicsModule, fixtures);
            return false;
        }
    }

    for(const MaterialResourceReference& resourceReference : materialInfo.resourceReferences){
        u32 heapSlot = 0u;
        switch(resourceReference.resourceKind){
        case MaterialResourceKind::SampledImage2D:
            if(resourceReference.fixtureName != Name(MaterialResourceFixture::s_CheckerRgba8)){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' requests unsupported sampled-image fixture")
                    , StringConvert(materialInfo.materialName.c_str())
                );
                return false;
            }
            heapSlot = fixtures.checkerRgba8HeapHandle.slot();
            break;
        case MaterialResourceKind::Sampler:
            if(resourceReference.fixtureName != Name(MaterialResourceFixture::s_LinearClamp)){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' requests unsupported sampler fixture")
                    , StringConvert(materialInfo.materialName.c_str())
                );
                return false;
            }
            heapSlot = fixtures.linearClampHeapHandle.slot();
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
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' resource fixture slot exceeds constant typed bytes")
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

    materialInfo.resourceFixturesResolved = true;
    return true;
}

void RendererMaterialSystem::releaseMaterialResourceFixtures(){
    __hidden_material_surface::ReleaseFixtureHeapHandles(graphics(), materialState().m_resourceFixtures);
    for(auto it = materialState().m_surfaceInfos.begin(); it != materialState().m_surfaceInfos.end(); ++it){
        MaterialSurfaceInfo& materialInfo = it.value();
        materialInfo.constantTypedBytes = materialInfo.unpatchedConstantTypedBytes;
        materialInfo.resourceFixturesResolved = false;
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
        return resolveMaterialResourceFixtures(*outInfo);
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
    if(!resolveMaterialResourceFixtures(createdInfo))
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

    outInfo = &foundInfo.value();
    // A device reset keeps the CPU material cache but deliberately clears its descriptor-backed fixture slots.
    // Find-only paths (notably the shadow/trace material context) must not observe those zeroed words before a
    // visible-material creation pass happens to revisit the cache.
    return resolveMaterialResourceFixtures(*outInfo);
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


NWB_IMPL_END


