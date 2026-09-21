// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"
#include "sampled_texture_collection.h"

#include <impl/assets_material/asset.h>

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
    const NotNull<const char*> emptyKindText,
    LoadFn&& loadResource,
    ReleaseFn&& releaseResource
){
    if(!assetRef.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material {} asset reference is empty"), StringConvert(emptyKindText.get()));
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
        MakeNotNull("Texture2D"),
        [&](TextureGpuResource& outResource, const Core::Assets::AssetRef<Texture>& assetRef, const Name& assetPath){
            if(!TextureAssetLoader::Load(
                outResource,
                assetRef,
                assetPath,
                graphics,
                assetManager,
                MakeNotNull(NWB_TEXT("RendererSystem"))
            ))
                return false;
            if(outResource.sampledImageHeapHandle.descriptorClass() != Core::GpuDescriptorClass::SampledImage){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: Texture2D asset '{}' has an incompatible texture dimension")
                    , StringConvert(assetPath.c_str())
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
            , StringConvert(texturePath.c_str())
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
        MakeNotNull("sampler"),
        [&](SamplerGpuResource& outResource, const Core::Assets::AssetRef<Sampler>& assetRef, const Name& assetPath){
            return SamplerAssetLoader::Load(
                outResource,
                assetRef,
                assetPath,
                graphics,
                assetManager,
                MakeNotNull(NWB_TEXT("RendererSystem"))
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
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cached sampler asset '{}' is invalid"), StringConvert(samplerPath.c_str()));
        return false;
    }

    outHeapSlot = samplerResource->samplerHeapHandle.slot();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_FixtureCheckerWidth = 2u;
static constexpr u32 s_FixtureCheckerHeight = 2u;
static constexpr u8 s_CheckerRgba8Pixels[] = {
    255u, 255u, 255u, 255u,  32u,  32u,  32u, 255u,
     32u,  32u,  32u, 255u, 255u, 255u, 255u, 255u,
};
static_assert(sizeof(s_CheckerRgba8Pixels) == s_FixtureCheckerWidth * s_FixtureCheckerHeight * sizeof(u32));
static void ReleaseFixtureHeapHandles(Core::GraphicsRuntime& graphics, RendererMaterialResourceFixtureState& fixtures){
    Core::GpuDescriptorHeap& heap = graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized()){
        if(fixtures.checkerRgba8HeapHandle.valid())
            heap.free(fixtures.checkerRgba8HeapHandle);
        if(fixtures.linearClampHeapHandle.valid())
            heap.free(fixtures.linearClampHeapHandle);
    }
    fixtures = RendererMaterialResourceFixtureState{};
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
        // The static first slice carries no per-material asset path; the fixture pass below patches its slot.
        if(resourceReference.fixtureName)
            continue;

        u32 heapSlot = 0u;
        if(resourceReference.resourceSource != MaterialResourceSource::Asset){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' has an invalid asset resource source"), StringConvert(materialInfo.materialName.c_str()));
            return false;
        }

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
                m_assetManager,
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
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' has an invalid material resource kind"), StringConvert(materialInfo.materialName.c_str()));
            return false;
        }

        if(
            resourceReference.constantByteOffset > materialInfo.constantTypedBytes.size()
            || sizeof(heapSlot) > materialInfo.constantTypedBytes.size() - resourceReference.constantByteOffset
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' resource slot exceeds constant typed bytes"), StringConvert(materialInfo.materialName.c_str()));
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


bool RendererMaterialSystem::resolveMaterialResourceFixtures(MaterialSurfaceInfo& materialInfo){
    if(materialInfo.resourceFixturesResolved)
        return true;

    // The asset pass above already patched per-material slots into constantTypedBytes; only patch static fixture slots here.
    if(materialInfo.resourceReferences.empty()){
        materialInfo.resourceFixturesResolved = true;
        return true;
    }

    RendererMaterialResourceFixtureState& fixtures = m_materialState.m_resourceFixtures;
    Core::GraphicsRuntime& graphicsModule = m_graphics;
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
            // The fixture is immutable after its Graphics upload, so concurrent sharing avoids a permanent ownership handoff for this common sampled input.
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setName(Name(MaterialResourceFixture::s_CheckerRgba8))
        ;
        Core::GraphicsRuntime::TextureSetupDesc textureSetup;
        textureSetup.textureDesc = textureDesc;
        textureSetup.data = __hidden_material_surface::s_CheckerRgba8Pixels;
        textureSetup.uploadDataSize = sizeof(__hidden_material_surface::s_CheckerRgba8Pixels);
        textureSetup.rowPitch = __hidden_material_surface::s_FixtureCheckerWidth * sizeof(u32);
        textureSetup.depthPitch = sizeof(__hidden_material_surface::s_CheckerRgba8Pixels);
        textureSetup.queue = Core::CommandQueue::Graphics;
        fixtures.checkerRgba8Texture = graphicsModule.setupTexture(textureSetup);
        if(!fixtures.checkerRgba8Texture){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create material checker texture fixture"));
            __hidden_material_surface::ReleaseFixtureHeapHandles(graphicsModule, fixtures);
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
        // Per-material asset paths are patched by the asset pass above; only the static slice lands here.
        if(!resourceReference.fixtureName)
            continue;

        u32 heapSlot = 0u;
        switch(resourceReference.resourceKind){
        case MaterialResourceKind::SampledImage2D:
            if(resourceReference.fixtureName != Name(MaterialResourceFixture::s_CheckerRgba8)){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' requests unsupported sampled-image fixture"), StringConvert(materialInfo.materialName.c_str()));
                return false;
            }
            heapSlot = fixtures.checkerRgba8HeapHandle.slot();
            break;
        case MaterialResourceKind::Sampler:
            if(resourceReference.fixtureName != Name(MaterialResourceFixture::s_LinearClamp)){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' requests unsupported sampler fixture"), StringConvert(materialInfo.materialName.c_str()));
                return false;
            }
            heapSlot = fixtures.linearClampHeapHandle.slot();
            break;
        default:
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' has an invalid material resource kind"), StringConvert(materialInfo.materialName.c_str()));
            return false;
        }

        if(
            resourceReference.constantByteOffset > materialInfo.constantTypedBytes.size()
            || sizeof(heapSlot) > materialInfo.constantTypedBytes.size() - resourceReference.constantByteOffset
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' resource fixture slot exceeds constant typed bytes"), StringConvert(materialInfo.materialName.c_str()));
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
    __hidden_material_surface::ReleaseFixtureHeapHandles(m_graphics, m_materialState.m_resourceFixtures);
    for(auto it = m_materialState.m_surfaceInfos.begin(); it != m_materialState.m_surfaceInfos.end(); ++it){
        MaterialSurfaceInfo& materialInfo = it.value();
        materialInfo.constantTypedBytes = materialInfo.unpatchedConstantTypedBytes;
        materialInfo.resourceFixturesResolved = false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererMaterialSystem::splitMaterialTypedBytesByClass(
    const Material& material,
    const Name& materialPath,
    MaterialTypedByteVector& outConstantTypedBytes,
    MaterialTypedByteVector& outMutableDefaultTypedBytes
){
    static_cast<void>(materialPath);
    outConstantTypedBytes.clear();
    outMutableDefaultTypedBytes.clear();

    const auto& packedTypedBytes = material.typedBlockBytes();
    usize sourceByteOffset = 0u;
    for(const MaterialTypedLayoutBlock& block : material.typedLayoutBlocks()){
        // Material::loadBinary already ran ValidateMaterialTypedLayout; keep a debug-only invariant here.
        NWB_ASSERT(IsValidMaterialBlockClass(block.blockClass));
        NWB_ASSERT((block.byteSize & (sizeof(u32) - 1u)) == 0u);
        NWB_ASSERT(sourceByteOffset <= packedTypedBytes.size() && block.byteSize <= packedTypedBytes.size() - sourceByteOffset);
        if(!IsValidMaterialBlockClass(block.blockClass)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' has invalid typed material block class"), StringConvert(materialPath.c_str()));
            return false;
        }
        if((block.byteSize & (sizeof(u32) - 1u)) != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material block size is not u32 aligned"), StringConvert(materialPath.c_str()));
            return false;
        }
        if(sourceByteOffset > packedTypedBytes.size() || block.byteSize > packedTypedBytes.size() - sourceByteOffset){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material block bytes exceed packed data"), StringConvert(materialPath.c_str()));
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
    // Material::loadBinary already validated the packed byte count against the cooked layout.
    NWB_ASSERT(sourceByteOffset == packedTypedBytes.size());
    if(sourceByteOffset != packedTypedBytes.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material layout size does not match packed data"), StringConvert(materialPath.c_str()));
        return false;
    }

    return true;
}

bool RendererMaterialSystem::createMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset, MaterialSurfaceInfo*& outInfo){
    outInfo = nullptr;

    const Name materialPath = materialAsset.name();
    NWB_ASSERT(materialPath);
    if(!materialPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer material is empty"));
        return false;
    }

    const auto foundInfo = m_materialState.m_surfaceInfos.find(materialPath);
    if(foundInfo != m_materialState.m_surfaceInfos.end()){
        outInfo = &foundInfo.value();
        if(!resolveMaterialResourceReferences(*outInfo))
            return false;
        return resolveMaterialResourceFixtures(*outInfo);
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    const Material* loadedMaterial = m_assetManager.loadTypedSync<Material>(
        materialPath,
        loadedAsset,
        MakeNotNull(NWB_TEXT("RendererMaterialSystem::createMaterialSurfaceInfo")),
        MakeNotNull(NWB_TEXT("RendererSystem")),
        MakeNotNull("material")
    );
    if(!loadedMaterial)
        return false;

    const Material& material = *loadedMaterial;
    const auto& typedBlockBytes = material.typedBlockBytes();

    MaterialSurfaceInfo createdInfo(m_arena);
    createdInfo.materialName = materialPath;
    // Material::loadBinary already rejected empty shader variants and missing material interfaces.
    NWB_ASSERT(!material.shaderVariant().empty());
    if(material.shaderVariant().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' has empty shader variant"), StringConvert(materialPath.c_str()));
        return false;
    }
    createdInfo.shaderVariant.reserve(material.shaderVariant().size());
    createdInfo.shaderVariant.assign(material.shaderVariant().data(), material.shaderVariant().size());

    const bool hasPixelShader = material.findShaderForStage(Core::ShaderType::PixelStage, createdInfo.pixelShader);
    const bool hasMeshShader = material.findShaderForStage(Core::ShaderType::MeshStage, createdInfo.meshShader);
    createdInfo.avboitAccumulatePixelShader = material.avboitAccumulatePixelShader();
    createdInfo.avboitOccupancyPixelShader = material.avboitOccupancyPixelShader();
    createdInfo.avboitExtinctionPixelShader = material.avboitExtinctionPixelShader();
    if(!hasMeshShader){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' is missing required mesh shader"), StringConvert(materialPath.c_str()));
        return false;
    }
    if(!hasPixelShader && !material.transparent()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: opaque material '{}' is missing required pixel shader"), StringConvert(materialPath.c_str()));
        return false;
    }

    // Material::loadBinary already validated the typed layout (hash, blocks, fields, bytes).
    NWB_ASSERT(material.typedLayoutHash() != 0u && !typedBlockBytes.empty());
    NWB_ASSERT(material.typedLayoutBlocks().size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(material.typedLayoutFields().size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(typedBlockBytes.size() <= static_cast<usize>(Limit<u32>::s_Max));
    if(!material.materialInterface()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' is missing required material interface"), StringConvert(materialPath.c_str()));
        return false;
    }
    createdInfo.materialInterface = material.materialInterface();
    if(material.typedLayoutHash() == 0u || typedBlockBytes.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' is missing typed material data"), StringConvert(materialPath.c_str()));
        return false;
    }
    if(material.typedLayoutBlocks().size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material block count exceeds u32 limits"), StringConvert(materialPath.c_str()));
        return false;
    }
    if(material.typedLayoutFields().size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material field count exceeds u32 limits"), StringConvert(materialPath.c_str()));
        return false;
    }
    if(typedBlockBytes.size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' typed material data exceeds u32 limits"), StringConvert(materialPath.c_str()));
        return false;
    }

    createdInfo.typedLayoutHash = material.typedLayoutHash();
    createdInfo.typedLayoutBlocks.reserve(material.typedLayoutBlocks().size());
    createdInfo.typedLayoutBlocks.assign(material.typedLayoutBlocks().begin(), material.typedLayoutBlocks().end());
    createdInfo.typedLayoutFields.reserve(material.typedLayoutFields().size());
    createdInfo.typedLayoutFields.assign(material.typedLayoutFields().begin(), material.typedLayoutFields().end());
    createdInfo.resourceReferences.reserve(material.resourceReferences().size());
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
    if(!resolveMaterialResourceFixtures(createdInfo))
        return false;
    createdInfo.shadingModelId = material.shadingModelId();
    createdInfo.shadowTransmittanceModelId = material.shadowTransmittanceModelId();
    // UINT_MAX marks explicit opaque shaders without a surface hook; keep them out of CSG clipping.
    createdInfo.csgCapSurfaceDispatchAvailable = createdInfo.shadowTransmittanceModelId != Limit<u32>::s_Max;
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
    // A device reset keeps the CPU material cache but deliberately clears its descriptor-backed fixture slots.
    // Find-only paths (notably the shadow/trace material context) must not observe those zeroed words before a visible-material creation pass happens to revisit the cache.
    return resolveMaterialResourceFixtures(*outInfo);
}

bool RendererMaterialSystem::appendPreparedMaterialSurfaceSampledTextures(
    const MaterialSurfaceInfo& materialInfo,
    MaterialSampledTextureCollector<Core::Alloc::ScratchArena>& collector){
    return AppendPreparedMaterialSurfaceSampledTextures(materialInfo, m_materialState.m_resourceState, m_materialState.m_resourceFixtures, collector);
}

bool RendererMaterialSystem::gatherPreparedMaterialPassSampledTextures(
    const MaterialPassDrawItems* const* const drawItemSets,
    const usize drawItemSetCount,
    Vector<Core::TextureHandle, Core::Alloc::ScratchArena>& outTextures,
    Core::Alloc::ScratchArena& scratchArena){
    return GatherPreparedMaterialPassSampledTextures(m_materialState.m_surfaceInfos, m_materialState.m_resourceState, m_materialState.m_resourceFixtures, drawItemSets, drawItemSetCount, outTextures, scratchArena);
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

