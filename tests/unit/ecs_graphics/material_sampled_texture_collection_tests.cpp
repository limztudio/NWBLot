// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/material/sampled_texture_collection.h>

#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_sampled_texture_collection_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using TestArena = Tests::TestArena<struct MaterialSampledTextureCollectionTestsTag>;
using TextureVector = Vector<Core::TextureHandle, Core::Alloc::GlobalArena>;
using ScratchTextureVector = Vector<Core::TextureHandle, Core::Alloc::ScratchArena>;

struct CollectionContext{
    TestArena testArena;
    Core::GraphicsAllocator graphicsAllocator{ testArena.arena };
    Core::Alloc::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    TextureVector textures{ testArena.arena };
    Vector<Core::Assets::AssetRef<Texture>, Core::Alloc::GlobalArena> textureAssets{ testArena.arena };
    RendererMaterialResourceState resources{ testArena.arena };
    MaterialSurfaceInfoMap materials{ 0u, Hasher<Name>{}, EqualTo<Name>{}, testArena.arena };

    [[nodiscard]] Core::TextureHandle makeTexture(const Name& identity){
        Core::TextureDesc description;
        description.setName(identity).setWidth(4u).setHeight(4u).setFormat(Core::Format::RGBA8_UNORM);
        Core::Texture* const texture = Tests::NewMetadataOnlyTexture(testArena.arena, context, allocator, description);
        return Core::TextureHandle(texture, Core::TextureHandle::deleter_type(&testArena.arena), AdoptRef);
    }

    [[nodiscard]] usize addTexture(const bool named = true, const Core::TextureHandle& alias = {}){
        const usize index = textures.size();
        char indexText[32u] = {};
        Core::Assets::AssetRef<Texture> asset;
        asset.virtualPath = DeriveName(Name("tests/material_texture_collection/asset"), FormatDecimal(index, indexText));
        Core::TextureHandle texture = alias ? alias : makeTexture(named ? asset.name() : NAME_NONE);
        auto resource = MakeUnique<TextureGpuResource>();
        resource->texture = texture;
        resource->format = Core::Format::RGBA8_UNORM;
        resource->sampledImageHeapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::SampledImage, static_cast<u32>(index));
        EXPECT_TRUE(resources.textureAssetCache.try_emplace(asset.name(), Move(resource)).second);
        textures.push_back(Move(texture));
        textureAssets.push_back(asset);
        return index;
    }

    [[nodiscard]] Name addMaterial(const InitializerList<usize> textureIndices){
        char indexText[32u] = {};
        const Name name = DeriveName(Name("tests/material_texture_collection/material"), FormatDecimal(materials.size(), indexText));
        const auto inserted = materials.try_emplace(name, testArena.arena);
        EXPECT_TRUE(inserted.second);
        MaterialSurfaceInfo& material = inserted.first.value();
        material.materialName = name;
        material.resourceReferencesResolved = true;
        material.resourceReferences.reserve(textureIndices.size());
        for(const usize index : textureIndices){
            MaterialResourceReference reference;
            reference.resourceKind = MaterialResourceKind::SampledImage2D;
            reference.resourceSource = MaterialResourceSource::Asset;
            reference.textureAsset = textureAssets[index];
            material.resourceReferences.push_back(reference);
        }
        return name;
    }

    [[nodiscard]] Name addMaterialRange(const usize count, const bool reverse = false){
        const Name name = addMaterial({});
        MaterialSurfaceInfo& material = materials.at(name);
        material.resourceReferences.reserve(count);
        for(usize index = 0u; index < count; ++index){
            MaterialResourceReference reference;
            reference.resourceKind = MaterialResourceKind::SampledImage2D;
            reference.resourceSource = MaterialResourceSource::Asset;
            reference.textureAsset = textureAssets[reverse ? count - 1u - index : index];
            material.resourceReferences.push_back(reference);
        }
        return name;
    }

    [[nodiscard]] bool appendShadow(
        const Name& materialName,
        ShadowMaterialSampledTextureCollector& collector){
        return collector.collect(
            materials.at(materialName),
            [&](const MaterialSurfaceInfo& material, MaterialSampledTextureCollector<Core::Alloc::ScratchArena>& pending){
                return AppendPreparedMaterialSurfaceSampledTextures(material, resources, pending);
            }
        );
    }
};

static void AppendDraw(MaterialPassDrawItemVector& draws, const Name& material){
    MaterialPassDrawItem draw;
    draw.pipelineKey.material = material;
    draws.push_back(Move(draw));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MaterialSampledTextureCollection, PassKeepsFirstHandleOrderAcrossMeshComputeSetsAndAssetAliases){
    CollectionContext context;
    for(usize index = 0u; index < 5u; ++index)
        ASSERT_EQ(context.addTexture(), index);
    ASSERT_EQ(context.addTexture(true, context.textures[2u]), 5u);
    const Name first = context.addMaterial({ 2u, 0u, 2u });
    const Name second = context.addMaterial({ 1u, 5u });
    const Name third = context.addMaterial({ 3u });
    Core::Alloc::ScratchArena scratch(Name("tests/material_texture_collection/pass_order"));
    MaterialPassDrawItems firstSet(scratch);
    MaterialPassDrawItems secondSet(scratch);
    AppendDraw(firstSet.meshDrawItems, first);
    AppendDraw(firstSet.computeDrawItems, second);
    AppendDraw(secondSet.meshDrawItems, third);
    AppendDraw(secondSet.computeDrawItems, first);
    const MaterialPassDrawItems* sets[] = { &firstSet, &secondSet };
    ScratchTextureVector output(scratch);
    output.push_back(context.textures[4u]);
    ASSERT_TRUE(GatherPreparedMaterialPassSampledTextures(context.materials, context.resources, sets, LengthOf(sets), output, scratch));
    ASSERT_EQ(output.size(), 4u);
    EXPECT_EQ(output[0u], context.textures[2u]);
    EXPECT_EQ(output[1u], context.textures[0u]);
    EXPECT_EQ(output[2u], context.textures[1u]);
    EXPECT_EQ(output[3u], context.textures[3u]);
}

TEST(MaterialSampledTextureCollection, PassFailureKeepsOnlyTheSequentialPrefixAndClearsPreviousOutput){
    CollectionContext context;
    ASSERT_EQ(context.addTexture(), 0u);
    ASSERT_EQ(context.addTexture(), 1u);
    const Name first = context.addMaterial({ 0u });
    const Name second = context.addMaterial({ 1u });
    Core::Alloc::ScratchArena scratch(Name("tests/material_texture_collection/pass_failure"));
    MaterialPassDrawItems firstSet(scratch);
    MaterialPassDrawItems secondSet(scratch);
    AppendDraw(firstSet.meshDrawItems, first);
    AppendDraw(secondSet.meshDrawItems, Name("tests/material_texture_collection/missing_material"));
    AppendDraw(secondSet.computeDrawItems, second);
    ScratchTextureVector output(scratch);
    const MaterialPassDrawItems* sets[] = { &firstSet, &secondSet };
    EXPECT_FALSE(GatherPreparedMaterialPassSampledTextures(context.materials, context.resources, sets, 2u, output, scratch));
    ASSERT_EQ(output.size(), 1u);
    EXPECT_EQ(output[0u], context.textures[0u]);
    sets[1u] = nullptr;
    EXPECT_FALSE(GatherPreparedMaterialPassSampledTextures(context.materials, context.resources, sets, 2u, output, scratch));
    ASSERT_EQ(output.size(), 1u);
    EXPECT_EQ(output[0u], context.textures[0u]);
    EXPECT_FALSE(GatherPreparedMaterialPassSampledTextures(context.materials, context.resources, nullptr, 1u, output, scratch));
    EXPECT_TRUE(output.empty());
    output.push_back(context.textures[1u]);
    EXPECT_TRUE(GatherPreparedMaterialPassSampledTextures(context.materials, context.resources, nullptr, 0u, output, scratch));
    EXPECT_TRUE(output.empty());
}

TEST(MaterialSampledTextureCollection, SurfaceRechecksEveryCurrentResourceAndPreservesItsValidatedPrefix){
    for(u32 failure = 0u; failure < 10u; ++failure){
        CollectionContext context;
        ASSERT_EQ(context.addTexture(), 0u);
        ASSERT_EQ(context.addTexture(), 1u);
        const Name materialName = context.addMaterial({ 0u, 1u });
        MaterialSurfaceInfo& material = context.materials.at(materialName);
        MaterialResourceReference& reference = material.resourceReferences[1u];
        TextureGpuResource& resource = *context.resources.textureAssetCache.at(context.textureAssets[1u].name());
        switch(failure){
        case 0u: material.resourceReferencesResolved = false; break;
        case 1u: reference.resourceKind = MaterialResourceKind::None; break;
        case 2u: reference.resourceSource = MaterialResourceSource::None; break;
        case 3u: reference.textureAsset.reset(); break;
        case 4u: EXPECT_EQ(context.resources.textureAssetCache.erase(reference.textureAsset.name()), 1u); break;
        case 5u: context.resources.textureAssetCache.at(reference.textureAsset.name()).reset(); break;
        case 6u: resource.texture = nullptr; break;
        case 7u: resource.sampledImageHeapHandle = Core::GpuDescriptorHandle::invalid(); break;
        case 8u: resource.sampledImageHeapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 1u); break;
        case 9u: resource.format = Core::Format::UNKNOWN; break;
        }
        Core::Alloc::ScratchArena scratch(Name("tests/material_texture_collection/resource_failure"));
        ScratchTextureVector output(scratch);
        MaterialSampledTextureCollector<Core::Alloc::ScratchArena> collector(output, scratch);
        EXPECT_FALSE(AppendPreparedMaterialSurfaceSampledTextures(material, context.resources, collector));
        ASSERT_EQ(output.size(), failure == 0u ? 0u : 1u);
        if(!output.empty())
            EXPECT_EQ(output[0u], context.textures[0u]);
    }
}

TEST(MaterialSampledTextureCollection, SamplersKeepTheirExistingSkipContractAndPassCollectionAcceptsUnnamedImages){
    CollectionContext context;
    ASSERT_EQ(context.addTexture(false), 0u);
    const Name materialName = context.addMaterial({ 0u, 0u });
    auto& material = context.materials.at(materialName);
    MaterialResourceReference sampler;
    sampler.resourceKind = MaterialResourceKind::Sampler;
    material.resourceReferences.insert(material.resourceReferences.begin() + 1u, sampler);
    Core::Alloc::ScratchArena scratch(Name("tests/material_texture_collection/sampler"));
    ScratchTextureVector output(scratch);
    MaterialSampledTextureCollector<Core::Alloc::ScratchArena> collector(output, scratch);
    ASSERT_TRUE(AppendPreparedMaterialSurfaceSampledTextures(material, context.resources, collector));
    ASSERT_EQ(output.size(), 1u);
    EXPECT_EQ(output[0u], context.textures[0u]);
    EXPECT_EQ(output[0u]->getCreationDescription().name, NAME_NONE);
}

TEST(MaterialSampledTextureCollection, ShadowResourceFailurePublishesNoPartOfTheMaterialAndReleasesTemporaryOwnership){
    CollectionContext context;
    for(usize index = 0u; index < 3u; ++index)
        ASSERT_EQ(context.addTexture(), index);
    const Name material = context.addMaterial({ 0u, 1u });
    context.resources.textureAssetCache.at(context.textureAssets[1u].name())->format = Core::Format::UNKNOWN;
    Core::Alloc::ScratchArena scratch(Name("tests/material_texture_collection/shadow_atomic"));
    TextureVector output(context.testArena.arena);
    output.push_back(context.textures[2u]);
    const auto firstReferences = context.textures[0u]->getReferenceCount();
    ShadowMaterialSampledTextureCollector collector(output, scratch);
    EXPECT_FALSE(context.appendShadow(material, collector));
    ASSERT_EQ(output.size(), 1u);
    EXPECT_EQ(output[0u], context.textures[2u]);
    EXPECT_EQ(context.textures[0u]->getReferenceCount(), firstReferences);
    context.resources.textureAssetCache.at(context.textureAssets[1u].name())->format = Core::Format::RGBA8_UNORM;
    ASSERT_TRUE(context.appendShadow(material, collector));
    ASSERT_EQ(output.size(), 3u);
    EXPECT_EQ(output[1u], context.textures[0u]);
    EXPECT_EQ(output[2u], context.textures[1u]);
    EXPECT_EQ(context.textures[0u]->getReferenceCount(), firstReferences + 1u);
}

TEST(MaterialSampledTextureCollection, ShadowMissingCreationNamePreservesOnlyTheEarlierNamedPrefix){
    CollectionContext context;
    ASSERT_EQ(context.addTexture(), 0u);
    ASSERT_EQ(context.addTexture(false), 1u);
    ASSERT_EQ(context.addTexture(), 2u);
    const Name material = context.addMaterial({ 0u, 0u, 1u, 2u });
    Core::Alloc::ScratchArena scratch(Name("tests/material_texture_collection/shadow_name"));
    TextureVector output(context.testArena.arena);
    const auto laterReferences = context.textures[2u]->getReferenceCount();
    ShadowMaterialSampledTextureCollector collector(output, scratch);
    EXPECT_FALSE(context.appendShadow(material, collector));
    ASSERT_EQ(output.size(), 1u);
    EXPECT_EQ(output[0u], context.textures[0u]);
    EXPECT_EQ(context.textures[2u]->getReferenceCount(), laterReferences);
    EXPECT_FALSE(context.appendShadow(material, collector));
    EXPECT_EQ(output.size(), 1u);
}

TEST(MaterialSampledTextureCollection, RepeatedMaterialCallsObserveReplacementHandlesAndInvalidatedDescriptors){
    CollectionContext context;
    ASSERT_EQ(context.addTexture(), 0u);
    const Name material = context.addMaterial({ 0u, 0u });
    Core::Alloc::ScratchArena scratch(Name("tests/material_texture_collection/current_resource"));
    TextureVector output(context.testArena.arena);
    ShadowMaterialSampledTextureCollector collector(output, scratch);
    ASSERT_TRUE(context.appendShadow(material, collector));
    const auto original = output[0u];
    TextureGpuResource& resource = *context.resources.textureAssetCache.at(context.textureAssets[0u].name());
    resource.sampledImageHeapHandle = Core::GpuDescriptorHandle::invalid();
    EXPECT_FALSE(context.appendShadow(material, collector));
    ASSERT_EQ(output.size(), 1u);
    resource.sampledImageHeapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::SampledImage, 0u);
    resource.texture = context.makeTexture(original->getCreationDescription().name);
    ASSERT_TRUE(context.appendShadow(material, collector));
    ASSERT_EQ(output.size(), 2u);
    EXPECT_EQ(output[0u], original);
    EXPECT_EQ(output[1u], resource.texture);
    EXPECT_NE(output[0u].get(), output[1u].get());
    context.materials.at(material).resourceReferencesResolved = false;
    EXPECT_FALSE(context.appendShadow(material, collector));
    EXPECT_EQ(output.size(), 2u);
}

TEST(MaterialSampledTextureCollection, SeparateHardwareAndSoftwareCollectionsRetainFirstOwnershipAcrossLargePrefixes){
    CollectionContext context;
    context.textures.reserve(80u);
    context.textureAssets.reserve(80u);
    for(usize index = 0u; index < 80u; ++index)
        ASSERT_EQ(context.addTexture(), index);
    Vector<Name, Core::Alloc::GlobalArena> materialNames(context.testArena.arena);
    materialNames.reserve(80u);
    for(usize index = 0u; index < 80u; ++index)
        materialNames.push_back(context.addMaterial({ index, index }));
    Core::Alloc::ScratchArena scratch(Name("tests/material_texture_collection/hybrid"));
    TextureVector output(context.testArena.arena);
    {
        ShadowMaterialSampledTextureCollector hardware(output, scratch);
        for(usize index = 0u; index < 48u; ++index)
            ASSERT_TRUE(context.appendShadow(materialNames[index], hardware));
    }
    {
        ShadowMaterialSampledTextureCollector software(output, scratch);
        for(usize index = 16u; index < 80u; ++index)
            ASSERT_TRUE(context.appendShadow(materialNames[index], software));
    }
    ASSERT_EQ(output.size(), 80u);
    for(usize index = 0u; index < output.size(); ++index)
        EXPECT_EQ(output[index], context.textures[index]);
    context.resources.textureAssetCache.clear();
    context.textures.clear();
    for(const auto& texture : output){
        EXPECT_EQ(texture->getReferenceCount(), 1u);
        EXPECT_EQ(texture->getCreationDescription().width, 4u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MaterialSampledTextureCollection, PromotedPassPreservesFirstPointersAndRevalidatesALaterSharedReference){
    CollectionContext context;
    context.textures.reserve(81u);
    context.textureAssets.reserve(81u);
    for(usize index = 0u; index < 80u; ++index)
        ASSERT_EQ(context.addTexture(), index);
    ASSERT_EQ(context.addTexture(true, context.textures[7u]), 80u);
    const Name first = context.addMaterialRange(80u, true);
    const Name alias = context.addMaterial({ 80u, 2u, 80u });
    Core::Alloc::ScratchArena scratch(Name("tests/material_texture_collection/promoted_pass"));
    MaterialPassDrawItems draws(scratch);
    AppendDraw(draws.meshDrawItems, first);
    AppendDraw(draws.computeDrawItems, alias);
    const MaterialPassDrawItems* sets[] = { &draws };
    ScratchTextureVector output(scratch);
    ASSERT_TRUE(GatherPreparedMaterialPassSampledTextures(context.materials, context.resources, sets, 1u, output, scratch));
    ASSERT_EQ(output.size(), 80u);
    for(usize index = 0u; index < output.size(); ++index)
        EXPECT_EQ(output[index], context.textures[79u - index]);

    context.resources.textureAssetCache.at(context.textureAssets[80u].name())->sampledImageHeapHandle =
        Core::GpuDescriptorHandle::invalid();
    EXPECT_FALSE(GatherPreparedMaterialPassSampledTextures(context.materials, context.resources, sets, 1u, output, scratch));
    ASSERT_EQ(output.size(), 80u);
    for(usize index = 0u; index < output.size(); ++index)
        EXPECT_EQ(output[index], context.textures[79u - index]);
}

TEST(MaterialSampledTextureCollection, ReusedPromotedPendingStorageClearsFailureOwnershipAndStabilizesOnOneArena){
    CollectionContext context;
    context.textures.reserve(80u);
    context.textureAssets.reserve(80u);
    for(usize index = 0u; index < 80u; ++index)
        ASSERT_EQ(context.addTexture(), index);
    const Name large = context.addMaterialRange(80u);
    const Name small = context.addMaterial({ 1u, 0u, 1u });
    Core::Alloc::ScratchArena scratch(Name("tests/material_texture_collection/pending_reuse"));
    TextureVector output(context.testArena.arena);
    output.reserve(80u);
    ShadowMaterialSampledTextureCollector collector(output, scratch);
    const auto initialReferences = context.textures[0u]->getReferenceCount();
    TextureGpuResource& lastResource = *context.resources.textureAssetCache.at(context.textureAssets[79u].name());
    lastResource.format = Core::Format::UNKNOWN;
    EXPECT_FALSE(context.appendShadow(large, collector));
    EXPECT_TRUE(output.empty());
    EXPECT_EQ(context.textures[0u]->getReferenceCount(), initialReferences);

    ASSERT_TRUE(context.appendShadow(small, collector));
    lastResource.format = Core::Format::RGBA8_UNORM;
    ASSERT_TRUE(context.appendShadow(large, collector));
    ASSERT_EQ(output.size(), 80u);
    EXPECT_EQ(output[0u], context.textures[1u]);
    EXPECT_EQ(output[1u], context.textures[0u]);
    for(usize index = 2u; index < output.size(); ++index)
        EXPECT_EQ(output[index], context.textures[index]);
    const auto warm = scratch.memoryStats();
    for(usize iteration = 0u; iteration < 8u; ++iteration){
        ASSERT_TRUE(context.appendShadow(small, collector));
        ASSERT_TRUE(context.appendShadow(large, collector));
    }
    const auto repeated = scratch.memoryStats();
    EXPECT_EQ(repeated.allocationCount, warm.allocationCount);
    EXPECT_EQ(repeated.usedBytes, warm.usedBytes);
    EXPECT_EQ(repeated.reservedBytes, warm.reservedBytes);
    for(const auto& texture : context.textures)
        EXPECT_EQ(texture->getReferenceCount(), initialReferences + 1u);
}

TEST(MaterialSampledTextureCollection, SeededDuplicatesRemainOrderedAndEmptyOrRepeatedSmallCollectionsNeedNoIndex){
    CollectionContext context;
    for(usize index = 0u; index < 8u; ++index)
        ASSERT_EQ(context.addTexture(), index);
    Core::Alloc::ScratchArena scratch(Name("tests/material_texture_collection/small_index"));
    TextureVector output(context.testArena.arena);
    output.reserve(8u);
    const auto empty = scratch.memoryStats();
    {
        ShadowMaterialSampledTextureCollector unused(output, scratch);
    }
    EXPECT_EQ(scratch.memoryStats().allocationCount, empty.allocationCount);
    output.push_back(context.textures[2u]);
    output.push_back(context.textures[0u]);
    output.push_back(context.textures[2u]);
    const auto prefixReferences = context.textures[2u]->getReferenceCount();
    {
        MaterialSampledTextureCollector<Core::Alloc::GlobalArena> collector(output, scratch);
        for(usize iteration = 0u; iteration < 1024u; ++iteration){
            collector.append(context.textures[2u]);
            collector.append(context.textures[1u]);
        }
    }
    ASSERT_EQ(output.size(), 4u);
    EXPECT_EQ(output[0u], context.textures[2u]);
    EXPECT_EQ(output[1u], context.textures[0u]);
    EXPECT_EQ(output[2u], context.textures[2u]);
    EXPECT_EQ(output[3u], context.textures[1u]);
    EXPECT_EQ(context.textures[2u]->getReferenceCount(), prefixReferences);
    EXPECT_EQ(scratch.memoryStats().allocationCount, empty.allocationCount);
}

TEST(MaterialSampledTextureCollection, UnexpectedResolverUnwindReleasesPendingHandlesBeforeCollectorDestruction){
    CollectionContext context;
    ASSERT_EQ(context.addTexture(), 0u);
    const Name material = context.addMaterial({ 0u, 0u });
    Core::Alloc::ScratchArena scratch(Name("tests/material_texture_collection/unwind"));
    TextureVector output(context.testArena.arena);
    ShadowMaterialSampledTextureCollector collector(output, scratch);
    const auto references = context.textures[0u]->getReferenceCount();
    bool unexpectedSuccess = false;
    EXPECT_THROW(
        unexpectedSuccess = collector.collect(
            context.materials.at(material),
            [&](const MaterialSurfaceInfo& info, MaterialSampledTextureCollector<Core::Alloc::ScratchArena>& pending){
                if(!AppendPreparedMaterialSurfaceSampledTextures(info, context.resources, pending))
                    return false;
                throw RuntimeException("terminal sampled texture resolver failure");
            }
        ),
        RuntimeException
    );
    EXPECT_FALSE(unexpectedSuccess);
    EXPECT_TRUE(output.empty());
    EXPECT_EQ(context.textures[0u]->getReferenceCount(), references);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

static void BenchmarkCollection(
    const usize uniqueTextures,
    const usize drawCount,
    const usize iterations,
    const bool shadow,
    const bool hybrid = false){
    CollectionContext context;
    context.textures.reserve(uniqueTextures);
    context.textureAssets.reserve(uniqueTextures);
    context.resources.textureAssetCache.reserve(uniqueTextures);
    context.materials.reserve(uniqueTextures);
    Vector<Name, Core::Alloc::GlobalArena> materialNames(context.testArena.arena);
    materialNames.reserve(uniqueTextures);
    for(usize index = 0u; index < uniqueTextures; ++index){
        ASSERT_EQ(context.addTexture(), index);
        materialNames.push_back(context.addMaterial({ index, index }));
    }
    Core::Alloc::ScratchArena setupScratch(Name("tests/material_texture_collection/benchmark_setup"));
    MaterialPassDrawItems drawItems(setupScratch);
    drawItems.meshDrawItems.reserve(drawCount);
    for(usize index = 0u; index < drawCount; ++index)
        AppendDraw(drawItems.meshDrawItems, materialNames[index % uniqueTextures]);
    const MaterialPassDrawItems* sets[] = { &drawItems };
    TextureVector shadowOutput(context.testArena.arena);
    u64 elapsed = 0u;
    u64 observedCount = 0u;
    u64 scratchPeak = 0u;
    u64 scratchReserved = 0u;
    bool success = true;
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        const Timer begin = TimerNow();
        {
            Core::Alloc::ScratchArena scratch(Name("tests/material_texture_collection/benchmark"));
            if(shadow){
                shadowOutput.clear();
                for(usize phase = 0u; phase < (hybrid ? 2u : 1u); ++phase){
                    ShadowMaterialSampledTextureCollector collector(shadowOutput, scratch);
                    for(usize index = 0u; index < drawCount; ++index){
                        if(!context.appendShadow(materialNames[index % uniqueTextures], collector)){
                            success = false;
                            break;
                        }
                    }
                }
                observedCount += shadowOutput.size();
            }
            else{
                ScratchTextureVector output(scratch);
                if(!GatherPreparedMaterialPassSampledTextures(context.materials, context.resources, sets, 1u, output, scratch))
                    success = false;
                observedCount += output.size();
            }
            scratchPeak = Max(scratchPeak, scratch.memoryStats().peakUsedBytes);
            scratchReserved = Max(scratchReserved, scratch.memoryStats().reservedBytes);
        }
        elapsed += DurationInNS<u64>(TimerNow(), begin);
    }
    EXPECT_TRUE(success);
    EXPECT_EQ(observedCount, uniqueTextures * iterations);
    RecordUnsignedProperty(MakeNotNull("material_texture_collection_ns"), elapsed);
    RecordUnsignedProperty(MakeNotNull("material_texture_unique_count"), uniqueTextures);
    RecordUnsignedProperty(MakeNotNull("material_texture_draw_count"), drawCount);
    RecordUnsignedProperty(MakeNotNull("material_texture_iterations"), iterations);
    RecordUnsignedProperty(MakeNotNull("material_texture_reference_count"), drawCount * 2u * (hybrid ? 2u : 1u) * iterations);
    RecordUnsignedProperty(MakeNotNull("material_texture_scratch_peak_bytes"), scratchPeak);
    RecordUnsignedProperty(MakeNotNull("material_texture_scratch_reserved_bytes"), scratchReserved);
}

TEST(MaterialSampledTextureCollectionBenchmark, DISABLED_PassSingleTexture){
    BenchmarkCollection(1u, 1u, 2048u, false);
}

TEST(MaterialSampledTextureCollectionBenchmark, DISABLED_PassEightTextures){
    BenchmarkCollection(8u, 8u, 256u, false);
}

TEST(MaterialSampledTextureCollectionBenchmark, DISABLED_PassUnique1024Textures){
    BenchmarkCollection(1024u, 1024u, 1u, false);
}

TEST(MaterialSampledTextureCollectionBenchmark, DISABLED_PassSharedEightTextures4096Draws){
    BenchmarkCollection(8u, 4096u, 2u, false);
}

TEST(MaterialSampledTextureCollectionBenchmark, DISABLED_ShadowSingleTexture){
    BenchmarkCollection(1u, 1u, 2048u, true);
}

TEST(MaterialSampledTextureCollectionBenchmark, DISABLED_ShadowEightTextures){
    BenchmarkCollection(8u, 8u, 256u, true);
}

TEST(MaterialSampledTextureCollectionBenchmark, DISABLED_ShadowUnique1024Textures){
    BenchmarkCollection(1024u, 1024u, 1u, true);
}

TEST(MaterialSampledTextureCollectionBenchmark, DISABLED_ShadowSharedEightTextures4096Instances){
    BenchmarkCollection(8u, 4096u, 2u, true);
}

TEST(MaterialSampledTextureCollectionBenchmark, DISABLED_HybridUnique128Textures){
    BenchmarkCollection(128u, 128u, 4u, true, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

