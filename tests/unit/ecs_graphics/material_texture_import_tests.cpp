// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/material/sampled_texture_graph_resources.h>
#include <core/graphics/vulkan/backend.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_texture_import_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using TestArena = Tests::TestArena<struct MaterialTextureImportTestsTag>;
using TextureVector = Vector<Core::TextureHandle, Core::Alloc::GlobalArena>;
using ResourceVector = Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>;

struct TextureContext{
    TestArena testArena;
    Core::GraphicsAllocator graphicsAllocator{ testArena.arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    Core::GpuTaskGraph graph{ testArena.arena };
    TextureVector textures{ testArena.arena };

    [[nodiscard]] Core::TextureHandle makeTexture(const Name& identity){
        Core::TextureDesc description;
        description.setName(identity).setWidth(4u).setHeight(4u).setFormat(Core::Format::RGBA8_UNORM);
        Core::Texture* const texture = Tests::NewMetadataOnlyTexture(testArena.arena, context, allocator, description);
        return Core::TextureHandle(texture, Core::TextureHandle::deleter_type(&testArena.arena), AdoptRef);
    }

    [[nodiscard]] Core::GpuGraphResourceId importExisting(const Core::TextureHandle& texture, const Name& identity){
        return graph.importTexture(texture, RendererTaskGraphDetail::TextureResourceDesc(identity, "Existing Material Texture"));
    }
};


TEST(MaterialTextureImport, PreservesRequestedOrderDuplicatesAndExistingMetadata){
    TextureContext context;
    const auto first = context.makeTexture(Name("tests/texture_import/first"));
    const auto second = context.makeTexture(Name("tests/texture_import/second"));
    const auto third = context.makeTexture(Name("tests/texture_import/third"));
    const auto existing = context.importExisting(second, Name("tests/texture_import/existing_alias"));
    ASSERT_TRUE(existing.valid());
    const Array<Core::TextureHandle, 5u> requested = { second, first, second, third, first };
    Core::Alloc::ScratchArena scratch(Name("tests/texture_import/scratch"));
    ResourceVector resources(scratch);
    ASSERT_EQ(ImportMaterialSampledTextureResources(context.graph, requested.data(), requested.size(), "New Material Texture", resources), SampledTextureImportResult::Success);
    ASSERT_EQ(resources.size(), 5u);
    EXPECT_EQ(resources[0u], existing);
    EXPECT_EQ(resources[0u], resources[2u]);
    EXPECT_EQ(resources[1u], resources[4u]);
    EXPECT_NE(resources[1u], resources[3u]);
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.resourceCount(), 3u);
    EXPECT_EQ(view.textureForResource(resources[1u]), first.get());
    EXPECT_EQ(view.textureForResource(resources[3u]), third.get());
    EXPECT_EQ(view.resourceAt(0u).identity, Name("tests/texture_import/existing_alias"));
    EXPECT_EQ(view.resourceAt(0u).markerLabel, "Existing Material Texture");
}

TEST(MaterialTextureImport, EmptyInputLeavesOutputAndUnnamedExistingTextureIntact){
    TextureContext context;
    const auto unnamed = context.makeTexture(NAME_NONE);
    const auto existing = context.importExisting(unnamed, Name("tests/texture_import/unnamed_alias"));
    ASSERT_TRUE(existing.valid());
    Core::Alloc::ScratchArena scratch(Name("tests/texture_import/scratch"));
    ResourceVector resources(scratch);
    resources.push_back(existing);
    EXPECT_EQ(ImportMaterialSampledTextureResources(context.graph, nullptr, 0u, "Unused", resources), SampledTextureImportResult::Success);
    ASSERT_EQ(resources.size(), 1u);
    EXPECT_EQ(ImportMaterialSampledTextureResources(context.graph, &unnamed, 1u, "New Label", resources), SampledTextureImportResult::Success);
    ASSERT_EQ(resources.size(), 2u);
    EXPECT_EQ(resources[1u], existing);
}

TEST(MaterialTextureImport, SingletonFailuresPreserveExistingOutputAndUseTheCompleteImportContract){
    for(u32 failureKind = 0u; failureKind < 4u; ++failureKind){
        TextureContext context;
        const Name prefixIdentity("tests/texture_import/singleton_prefix");
        const auto prefixTexture = context.makeTexture(prefixIdentity);
        const auto prefix = context.importExisting(prefixTexture, prefixIdentity);
        ASSERT_TRUE(prefix.valid());
        Core::TextureHandle texture;
        if(failureKind != 0u){
            const Name identity = failureKind == 1u
                ? NAME_NONE
                : failureKind == 2u ? prefixIdentity : Name("tests/texture_import/singleton_changed")
            ;
            texture = context.makeTexture(identity);
        }
        if(failureKind == 3u){
            Core::TextureDesc& description = const_cast<Core::TextureDesc&>(texture->getDescription());
            ++description.width;
            ASSERT_FALSE(texture->descriptionMatchesCreation());
        }
        Core::Alloc::ScratchArena scratch(Name("tests/texture_import/singleton_failure"));
        ResourceVector resources(scratch);
        resources.push_back(prefix);
        const auto expected = failureKind < 2u
            ? SampledTextureImportResult::MissingIdentity
            : SampledTextureImportResult::ImportFailed
        ;
        EXPECT_EQ(ImportMaterialSampledTextureResources(
            context.graph, &texture, 1u, "Material Texture", resources
        ), expected);
        ASSERT_EQ(resources.size(), 1u);
        EXPECT_EQ(resources.front(), prefix);
        const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
        ASSERT_TRUE(view.valid());
        EXPECT_EQ(view.resourceCount(), 1u);
        EXPECT_EQ(view.textureForResource(prefix), prefixTexture.get());
        EXPECT_FALSE(view.findImportedTexture(texture).valid());
        if(failureKind == 3u){
            Core::TextureDesc& description = const_cast<Core::TextureDesc&>(texture->getDescription());
            description = texture->getCreationDescription();
        }
    }
}

TEST(MaterialTextureImport, MissingIdentityKeepsTheImportedPrefixAndDoesNotProcessLaterTextures){
    for(const bool useNull : { false, true }){
        TextureContext context;
        const Array<Core::TextureHandle, 3u> requested = {
            context.makeTexture(Name("tests/texture_import/prefix")),
            useNull ? Core::TextureHandle{} : context.makeTexture(NAME_NONE),
            context.makeTexture(Name("tests/texture_import/later")),
        };
        Core::Alloc::ScratchArena scratch(Name("tests/texture_import/scratch"));
        ResourceVector resources(scratch);
        EXPECT_EQ(ImportMaterialSampledTextureResources(context.graph, requested.data(), requested.size(), "Material Texture", resources), SampledTextureImportResult::MissingIdentity);
        ASSERT_EQ(resources.size(), 1u);
        const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
        ASSERT_TRUE(view.valid());
        EXPECT_EQ(view.resourceCount(), 1u);
        EXPECT_EQ(view.textureForResource(resources[0u]), requested[0u].get());
        EXPECT_FALSE(view.findImportedTexture(requested[2u]).valid());
    }
}

TEST(MaterialTextureImport, IdentityConflictIsAnImportFailureAfterTheValidPrefix){
    TextureContext context;
    const Name identity("tests/texture_import/conflict");
    const auto existingTexture = context.makeTexture(identity);
    ASSERT_TRUE(context.importExisting(existingTexture, identity).valid());
    const Array<Core::TextureHandle, 3u> requested = {
        context.makeTexture(Name("tests/texture_import/prefix")), context.makeTexture(identity),
        context.makeTexture(Name("tests/texture_import/later")),
    };
    Core::Alloc::ScratchArena scratch(Name("tests/texture_import/scratch"));
    ResourceVector resources(scratch);
    EXPECT_EQ(ImportMaterialSampledTextureResources(context.graph, requested.data(), requested.size(), "Material Texture", resources), SampledTextureImportResult::ImportFailed);
    ASSERT_EQ(resources.size(), 1u);
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.resourceCount(), 2u);
    EXPECT_FALSE(view.findImportedTexture(requested[1u]).valid());
    EXPECT_FALSE(view.findImportedTexture(requested[2u]).valid());
}

TEST(MaterialTextureImport, NewCallAfterResetUsesCurrentGenerationAndReplacementHandle){
    TextureContext context;
    const Name identity("tests/texture_import/replacement");
    Core::TextureHandle texture = context.makeTexture(identity);
    Core::Alloc::ScratchArena scratch(Name("tests/texture_import/scratch"));
    ResourceVector resources(scratch);
    ASSERT_EQ(ImportMaterialSampledTextureResources(context.graph, &texture, 1u, "Material Texture", resources), SampledTextureImportResult::Success);
    const auto oldResource = resources[0u];
    context.graph.reset();
    texture = context.makeTexture(identity);
    resources.clear();
    ASSERT_EQ(ImportMaterialSampledTextureResources(context.graph, &texture, 1u, "Material Texture", resources), SampledTextureImportResult::Success);
    ASSERT_EQ(resources.size(), 1u);
    EXPECT_NE(resources[0u].generation, oldResource.generation);
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_FALSE(view.validResource(oldResource));
    EXPECT_EQ(view.textureForResource(resources[0u]), texture.get());
}

TEST(MaterialTextureImport, LargeRequestsPreserveAliasesOrderAndOwnershipWhileImportingMissingTextures){
    TextureContext context;
    constexpr usize s_TextureCount = 40u;
    Array<Core::GpuGraphResourceId, s_TextureCount> existing;
    for(usize index = 0u; index < s_TextureCount; ++index){
        char indexText[32u] = {};
        const Name identity = index == 0u
            ? NAME_NONE
            : DeriveName(Name("tests/texture_import/promoted"), FormatDecimal(index, indexText))
        ;
        context.textures.push_back(context.makeTexture(identity));
    }
    for(usize remaining = s_TextureCount; remaining != 0u; --remaining){
        const usize index = remaining - 1u;
        if(index % 2u != 0u)
            continue;
        char indexText[32u] = {};
        const Name alias = DeriveName(Name("tests/texture_import/promoted_alias"), FormatDecimal(index, indexText));
        existing[index] = context.importExisting(context.textures[index], alias);
        ASSERT_TRUE(existing[index].valid());
    }
    Core::TextureDesc& existingDescription = const_cast<Core::TextureDesc&>(context.textures[0u]->getDescription());
    ++existingDescription.width;
    ASSERT_FALSE(context.textures[0u]->descriptionMatchesCreation());
    TextureVector requests(context.testArena.arena);
    for(usize index = 0u; index < s_TextureCount; ++index){
        const auto& texture = context.textures[(index * 17u) % s_TextureCount];
        requests.push_back(texture);
        requests.push_back(texture);
    }
    Array<u32, s_TextureCount> referencesBefore;
    for(usize index = 0u; index < s_TextureCount; ++index)
        referencesBefore[index] = context.textures[index]->getReferenceCount();
    Core::Alloc::ScratchArena scratch(Name("tests/texture_import/promoted_scratch"));
    ResourceVector resources(scratch);
    resources.push_back(existing[0u]);
    ASSERT_EQ(ImportMaterialSampledTextureResources(
        context.graph, requests.data(), requests.size(), "New Material Texture", resources
    ), SampledTextureImportResult::Success);
    ASSERT_EQ(resources.size(), requests.size() + 1u);
    EXPECT_EQ(resources[0u], existing[0u]);
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.resourceCount(), s_TextureCount);
    for(usize index = 0u; index < s_TextureCount; ++index){
        const usize textureIndex = (index * 17u) % s_TextureCount;
        const auto resource = resources[index * 2u + 1u];
        EXPECT_EQ(resource, resources[index * 2u + 2u]);
        EXPECT_EQ(view.textureForResource(resource), context.textures[textureIndex].get());
        if(existing[textureIndex].valid()){
            EXPECT_EQ(resource, existing[textureIndex]);
            EXPECT_EQ(view.resourceAt(resource.index).markerLabel, "Existing Material Texture");
        }
        else
            EXPECT_EQ(view.resourceAt(resource.index).markerLabel, "New Material Texture");
        EXPECT_EQ(
            context.textures[textureIndex]->getReferenceCount(),
            referencesBefore[textureIndex] + (existing[textureIndex].valid() ? 0u : 1u)
        );
    }
    existingDescription = context.textures[0u]->getCreationDescription();
}

TEST(MaterialTextureImport, PromotedFailuresKeepOnlyTheAcceptedPrefixAndDoNotSkipImportValidation){
    constexpr usize s_TextureCount = 40u;
    constexpr usize s_FailureIndex = 35u;
    for(u32 failureKind = 0u; failureKind < 4u; ++failureKind){
        TextureContext context;
        for(usize index = 0u; index < s_TextureCount; ++index){
            char indexText[32u] = {};
            const Name identity = DeriveName(Name("tests/texture_import/failure"), FormatDecimal(index, indexText));
            context.textures.push_back(context.makeTexture(identity));
        }
        Core::TextureHandle conflictingTexture;
        if(failureKind == 0u)
            context.textures[s_FailureIndex] = nullptr;
        else if(failureKind == 1u)
            context.textures[s_FailureIndex] = context.makeTexture(NAME_NONE);
        else if(failureKind == 2u){
            const Name identity = context.textures[s_FailureIndex]->getCreationDescription().name;
            conflictingTexture = context.makeTexture(identity);
            ASSERT_TRUE(context.importExisting(conflictingTexture, identity).valid());
        }
        else{
            Core::TextureDesc& description = const_cast<Core::TextureDesc&>(
                context.textures[s_FailureIndex]->getDescription()
            );
            ++description.width;
            ASSERT_FALSE(context.textures[s_FailureIndex]->descriptionMatchesCreation());
        }
        Core::Alloc::ScratchArena scratch(Name("tests/texture_import/failure_scratch"));
        ResourceVector resources(scratch);
        const auto expected = failureKind < 2u
            ? SampledTextureImportResult::MissingIdentity
            : SampledTextureImportResult::ImportFailed
        ;
        EXPECT_EQ(ImportMaterialSampledTextureResources(
            context.graph, context.textures.data(), context.textures.size(), "Material Texture", resources
        ), expected);
        ASSERT_EQ(resources.size(), s_FailureIndex);
        const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
        ASSERT_TRUE(view.valid());
        EXPECT_EQ(view.resourceCount(), s_FailureIndex + (conflictingTexture ? 1u : 0u));
        for(usize index = 0u; index < s_FailureIndex; ++index)
            EXPECT_EQ(view.textureForResource(resources[index]), context.textures[index].get());
        for(usize index = s_FailureIndex; index < s_TextureCount; ++index)
            EXPECT_FALSE(view.findImportedTexture(context.textures[index]).valid());
        if(failureKind == 3u){
            Core::TextureDesc& description = const_cast<Core::TextureDesc&>(
                context.textures[s_FailureIndex]->getDescription()
            );
            description = context.textures[s_FailureIndex]->getCreationDescription();
        }
    }
}

TEST(MaterialTextureImport, PromotedCallsRebuildAfterResetAndLeaveTextureOwnershipWithTheGraph){
    TextureContext context;
    constexpr usize s_TextureCount = 40u;
    for(usize index = 0u; index < s_TextureCount; ++index){
        char indexText[32u] = {};
        const Name identity = DeriveName(Name("tests/texture_import/retained"), FormatDecimal(index, indexText));
        context.textures.push_back(context.makeTexture(identity));
    }
    Core::Alloc::ScratchArena scratch(Name("tests/texture_import/retained_scratch"));
    ResourceVector resources(scratch);
    ASSERT_EQ(ImportMaterialSampledTextureResources(
        context.graph, context.textures.data(), context.textures.size(), "Material Texture", resources
    ), SampledTextureImportResult::Success);
    ASSERT_EQ(resources.size(), s_TextureCount);
    const auto oldResource = resources[7u];
    const Core::TextureHandle retired = context.textures[7u];
    const Name replacementIdentity = retired->getCreationDescription().name;
    context.graph.reset();
    context.textures[7u] = context.makeTexture(replacementIdentity);
    Swap(context.textures.front(), context.textures.back());
    resources.clear();
    EXPECT_EQ(retired->getReferenceCount(), 1u);
    ASSERT_EQ(ImportMaterialSampledTextureResources(
        context.graph, context.textures.data(), context.textures.size(), "Replacement Material Texture", resources
    ), SampledTextureImportResult::Success);
    ASSERT_EQ(resources.size(), s_TextureCount);
    Array<Core::Texture*, s_TextureCount> retained;
    for(usize index = 0u; index < s_TextureCount; ++index){
        retained[index] = context.textures[index].get();
        EXPECT_NE(resources[index].generation, oldResource.generation);
        EXPECT_EQ(retained[index]->getReferenceCount(), 2u);
    }
    context.textures.clear();
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_FALSE(view.validResource(oldResource));
    EXPECT_FALSE(view.findImportedTexture(retired).valid());
    for(usize index = 0u; index < s_TextureCount; ++index){
        EXPECT_EQ(view.textureForResource(resources[index]), retained[index]);
        EXPECT_EQ(retained[index]->getReferenceCount(), 1u);
    }
}

TEST(MaterialTextureImport, ExistingTexturesRequireNoStorageBeyondReservedOrderedOutput){
    struct Workload{
        usize uniqueTextures;
        usize requests;
        usize unrelatedTextures;
    };
    const Workload workloads[] = {
        { 1u, 1u, 0u },
        { 1u, 4096u, 512u },
        { 32u, 32u, 0u },
        { 32u, 4096u, 512u },
        { 40u, 40u, 0u },
        { 40u, 4096u, 512u },
        { 40u, 40u, 512u },
    };
    for(usize workloadIndex = 0u; workloadIndex < LengthOf(workloads); ++workloadIndex){
        const Workload& workload = workloads[workloadIndex];
        TextureContext context;
        for(usize index = 0u; index < workload.uniqueTextures + workload.unrelatedTextures; ++index){
            char indexText[32u] = {};
            const Name identity = DeriveName(Name("tests/texture_import/scratch_shape"), FormatDecimal(index, indexText));
            context.textures.push_back(context.makeTexture(identity));
            ASSERT_TRUE(context.importExisting(context.textures.back(), identity).valid());
        }
        TextureVector requests(context.testArena.arena);
        requests.reserve(workload.requests);
        for(usize index = 0u; index < workload.requests; ++index)
            requests.push_back(context.textures[workload.unrelatedTextures + index % workload.uniqueTextures]);
        Core::Alloc::ScratchArena outputScratch(Name("tests/texture_import/output_scratch"));
        ResourceVector resources(outputScratch);
        resources.reserve(requests.size());
        const ArenaMemoryStats before = outputScratch.memoryStats();
        ASSERT_EQ(ImportMaterialSampledTextureResources(
            context.graph, requests.data(), requests.size(), "Material Texture", resources
        ), SampledTextureImportResult::Success);
        EXPECT_EQ(resources.size(), requests.size());
        const ArenaMemoryStats after = outputScratch.memoryStats();
        EXPECT_EQ(after.allocationCount, before.allocationCount);
        EXPECT_EQ(after.usedBytes, before.usedBytes);
        EXPECT_EQ(after.reservedBytes, before.reservedBytes);
        EXPECT_EQ(after.peakUsedBytes, before.peakUsedBytes);
        const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
        ASSERT_TRUE(view.valid());
        for(usize index = 0u; index < requests.size(); ++index)
            EXPECT_EQ(view.textureForResource(resources[index]), requests[index].get());
    }
}

static void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

static void BenchmarkTextureImports(
    const usize uniqueCount,
    const usize requestCount,
    const usize unrelatedCount,
    const usize iterations,
    const bool preimportRequested = true){
    TextureContext context;
    context.textures.reserve(uniqueCount + unrelatedCount);
    TextureVector requests(context.testArena.arena);
    requests.reserve(requestCount);
    for(usize index = 0u; index < uniqueCount + unrelatedCount; ++index){
        char indexText[32u] = {};
        const Name identity = DeriveName(Name("tests/texture_import/benchmark"), FormatDecimal(index, indexText));
        context.textures.push_back(context.makeTexture(identity));
        if(index < unrelatedCount || preimportRequested)
            ASSERT_TRUE(context.importExisting(context.textures.back(), identity).valid());
    }
    for(usize index = 0u; index < requestCount; ++index)
        requests.push_back(context.textures[unrelatedCount + index % uniqueCount]);
    u64 elapsed = 0u;
    u64 peak = 0u;
    u64 reserved = 0u;
    usize successes = 0u;
    usize resolved = 0u;
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        if(!preimportRequested && iteration != 0u){
            context.graph.reset();
            for(usize index = 0u; index < unrelatedCount; ++index){
                const auto& texture = context.textures[index];
                ASSERT_TRUE(context.importExisting(texture, texture->getCreationDescription().name).valid());
            }
        }
        Core::Alloc::ScratchArena scratch(Name("tests/texture_import/benchmark_scratch"));
        const Timer begin = TimerNow();
        {
            ResourceVector resources(scratch);
            resources.reserve(requests.size());
            if(ImportMaterialSampledTextureResources(context.graph, requests.data(), requests.size(), "Material Texture", resources) == SampledTextureImportResult::Success)
                ++successes;
            resolved += resources.size();
        }
        elapsed += DurationInNS<u64>(TimerNow(), begin);
        peak = Max(peak, scratch.memoryStats().peakUsedBytes);
        reserved = Max(reserved, scratch.memoryStats().reservedBytes);
    }
    EXPECT_EQ(successes, iterations);
    EXPECT_EQ(resolved, requestCount * iterations);
    Core::Alloc::ScratchArena scratch(Name("tests/texture_import/assertion_scratch"));
    ResourceVector resources(scratch);
    ASSERT_EQ(ImportMaterialSampledTextureResources(context.graph, requests.data(), requests.size(), "Material Texture", resources), SampledTextureImportResult::Success);
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.resourceCount(), uniqueCount + unrelatedCount);
    for(usize index = 0u; index < resources.size(); ++index)
        EXPECT_EQ(view.textureForResource(resources[index]), requests[index].get());
    RecordUnsignedProperty(MakeNotNull("material_texture_import_ns"), elapsed);
    RecordUnsignedProperty(MakeNotNull("material_texture_import_iterations"), iterations);
    RecordUnsignedProperty(MakeNotNull("material_texture_import_request_count"), requestCount);
    RecordUnsignedProperty(MakeNotNull("material_texture_import_unique_count"), uniqueCount);
    RecordUnsignedProperty(MakeNotNull("material_texture_import_unrelated_count"), unrelatedCount);
    RecordUnsignedProperty(MakeNotNull("material_texture_import_initially_missing"), preimportRequested ? 0u : uniqueCount);
    RecordUnsignedProperty(MakeNotNull("material_texture_import_scratch_peak_bytes"), peak);
    RecordUnsignedProperty(MakeNotNull("material_texture_import_scratch_reserved_bytes"), reserved);
}

TEST(MaterialTextureImportBenchmark, DISABLED_Unique1){
    BenchmarkTextureImports(1u, 1u, 0u, 2048u);
}

TEST(MaterialTextureImportBenchmark, DISABLED_Unique8){
    BenchmarkTextureImports(8u, 8u, 0u, 256u);
}

TEST(MaterialTextureImportBenchmark, DISABLED_Unique32){
    BenchmarkTextureImports(32u, 32u, 0u, 64u);
}

TEST(MaterialTextureImportBenchmark, DISABLED_Unique1024){
    BenchmarkTextureImports(1024u, 1024u, 1024u, 3u);
}

TEST(MaterialTextureImportBenchmark, DISABLED_Shared4096){
    BenchmarkTextureImports(1u, 4096u, 128u, 3u);
}

TEST(MaterialTextureImportBenchmark, DISABLED_Missing256){
    BenchmarkTextureImports(256u, 256u, 256u, 3u, false);
}

TEST(MaterialTextureImportBenchmark, DISABLED_Sparse1In4096){
    BenchmarkTextureImports(1u, 1u, 4096u, 32u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

