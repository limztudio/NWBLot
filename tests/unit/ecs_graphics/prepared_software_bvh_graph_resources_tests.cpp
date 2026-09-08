// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/prepared_software_bvh_graph_resources.h>

#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_prepared_software_bvh_graph_resources_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using TestArena = Tests::TestArena<struct PreparedSoftwareBvhGraphResourcesTestsTag>;
using BuildBufferMember = Core::BufferHandle PreparedMeshSwBvhBuild::*;
using GraphResourceMember = Core::GpuGraphResourceId PreparedMeshSwBvhGraphResources::*;

inline constexpr BuildBufferMember s_BuildBuffers[]{
    &PreparedMeshSwBvhBuild::positionBuffer,
    &PreparedMeshSwBvhBuild::triangleIndexBuffer,
    &PreparedMeshSwBvhBuild::nodeBuffer,
    &PreparedMeshSwBvhBuild::parentBuffer,
    &PreparedMeshSwBvhBuild::sortKeysBuffer,
    &PreparedMeshSwBvhBuild::sortPayloadBuffer,
    &PreparedMeshSwBvhBuild::visitCounterBuffer,
};
inline constexpr GraphResourceMember s_GraphResources[]{
    &PreparedMeshSwBvhGraphResources::position,
    &PreparedMeshSwBvhGraphResources::triangleIndex,
    &PreparedMeshSwBvhGraphResources::node,
    &PreparedMeshSwBvhGraphResources::parent,
    &PreparedMeshSwBvhGraphResources::sortKeys,
    &PreparedMeshSwBvhGraphResources::sortPayload,
    &PreparedMeshSwBvhGraphResources::visitCounter,
};
static_assert(LengthOf(s_BuildBuffers) == LengthOf(s_GraphResources));

struct BuildContext{
    TestArena testArena;
    Core::GraphicsAllocator graphicsAllocator{ testArena.arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    Core::GpuTaskGraph graph{ testArena.arena };
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> buffers{ testArena.arena };
    PreparedMeshSwBvhBuildVector builds{ testArena.arena };

    [[nodiscard]] Core::BufferHandle makeBuffer(const Name& identity){
        Core::Buffer* const buffer = Tests::NewMetadataOnlyBuffer(
            testArena.arena, context, allocator, Core::BufferDesc{}.setByteSize(256u).setDebugName(identity)
        );
        return Core::BufferHandle(buffer, Core::BufferHandle::deleter_type(&testArena.arena), AdoptRef);
    }

    [[nodiscard]] Core::GpuGraphResourceId importBuffer(const Core::BufferHandle& buffer, const Name& identity){
        return graph.importBuffer(
            buffer,
            Core::GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel("Prepared Software BVH Buffer")
                .setType(Core::GpuGraphResourceType::Buffer)
        );
    }

    void addBuffer(const bool import = true){
        char indexText[32u] = {};
        const Name identity = DeriveName(Name("tests/prepared_sw_bvh/buffer"), FormatDecimal(buffers.size(), indexText));
        buffers.push_back(makeBuffer(identity));
        if(import)
            EXPECT_TRUE(importBuffer(buffers.back(), identity).valid());
    }

    void addBuild(const usize privateFirst, const usize sharedFirst, const usize ordinal){
        char indexText[32u] = {};
        PreparedMeshSwBvhBuild build{};
        build.meshName = DeriveName(Name("tests/prepared_sw_bvh/mesh"), FormatDecimal(ordinal, indexText));
        build.positionBuffer = buffers[privateFirst];
        build.triangleIndexBuffer = buffers[privateFirst + 1u];
        build.nodeBuffer = buffers[privateFirst + 2u];
        build.parentBuffer = buffers[privateFirst + 3u];
        build.sortKeysBuffer = buffers[sharedFirst];
        build.sortPayloadBuffer = buffers[sharedFirst + 1u];
        build.visitCounterBuffer = buffers[sharedFirst + 2u];
        build.positionHeapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 1u);
        build.triangleIndexHeapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 2u);
        build.nodeHeapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 3u);
        build.parentHeapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 4u);
        build.sortKeysHeapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 5u);
        build.sortPayloadHeapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 6u);
        build.visitCounterHeapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 7u);
        build.aabbMin = Float3Int(-3.f, -2.f, -1.f, 19);
        build.aabbMax = Float3Int(1.f, 2.f, 3.f, 23);
        build.runtimeMeshVersion = 0x100000000ull + ordinal;
        build.positionByteSize = 32u + ordinal;
        build.indexByteSize = 64u + ordinal;
        build.nodeByteSize = 96u + ordinal;
        build.parentByteSize = 128u + ordinal;
        build.sortKeysByteSize = 160u + ordinal;
        build.sortPayloadByteSize = 192u + ordinal;
        build.visitCounterByteSize = 224u + ordinal;
        build.primitiveCount = static_cast<u32>(ordinal + 5u);
        build.refitsBeforeBuild = static_cast<u32>(ordinal + 2u);
        build.refitsAfterBuild = static_cast<u32>(ordinal + 3u);
        build.runtimeMesh = ordinal % 2u != 0u;
        build.buildPending = ordinal % 3u != 0u;
        build.firstBuild = ordinal % 5u != 0u;
        build.performRefit = ordinal % 7u != 0u;
        builds.push_back(Move(build));
    }
};


static void ExpectBuildSnapshot(const PreparedMeshSwBvhBuild& actual, const PreparedMeshSwBvhBuild& expected){
    EXPECT_EQ(actual.meshName, expected.meshName);
    for(const BuildBufferMember member : s_BuildBuffers)
        EXPECT_EQ(actual.*member, expected.*member);
    EXPECT_EQ(actual.positionHeapHandle, expected.positionHeapHandle);
    EXPECT_EQ(actual.triangleIndexHeapHandle, expected.triangleIndexHeapHandle);
    EXPECT_EQ(actual.nodeHeapHandle, expected.nodeHeapHandle);
    EXPECT_EQ(actual.parentHeapHandle, expected.parentHeapHandle);
    EXPECT_EQ(actual.sortKeysHeapHandle, expected.sortKeysHeapHandle);
    EXPECT_EQ(actual.sortPayloadHeapHandle, expected.sortPayloadHeapHandle);
    EXPECT_EQ(actual.visitCounterHeapHandle, expected.visitCounterHeapHandle);
    EXPECT_EQ(actual.aabbMin, expected.aabbMin);
    EXPECT_EQ(actual.aabbMax, expected.aabbMax);
    EXPECT_EQ(actual.runtimeMeshVersion, expected.runtimeMeshVersion);
    EXPECT_EQ(actual.positionByteSize, expected.positionByteSize);
    EXPECT_EQ(actual.indexByteSize, expected.indexByteSize);
    EXPECT_EQ(actual.nodeByteSize, expected.nodeByteSize);
    EXPECT_EQ(actual.parentByteSize, expected.parentByteSize);
    EXPECT_EQ(actual.sortKeysByteSize, expected.sortKeysByteSize);
    EXPECT_EQ(actual.sortPayloadByteSize, expected.sortPayloadByteSize);
    EXPECT_EQ(actual.visitCounterByteSize, expected.visitCounterByteSize);
    EXPECT_EQ(actual.primitiveCount, expected.primitiveCount);
    EXPECT_EQ(actual.refitsBeforeBuild, expected.refitsBeforeBuild);
    EXPECT_EQ(actual.refitsAfterBuild, expected.refitsAfterBuild);
    EXPECT_EQ(actual.runtimeMesh, expected.runtimeMesh);
    EXPECT_EQ(actual.buildPending, expected.buildPending);
    EXPECT_EQ(actual.firstBuild, expected.firstBuild);
    EXPECT_EQ(actual.performRefit, expected.performRefit);
}

static void ExpectRows(const BuildContext& context, const PreparedMeshSwBvhGraphResourceVector& resources){
    ASSERT_EQ(resources.size(), context.builds.size());
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    for(usize index = 0u; index < resources.size(); ++index){
        ASSERT_NO_FATAL_FAILURE(ExpectBuildSnapshot(resources[index].build, context.builds[index]));
        for(usize role = 0u; role < LengthOf(s_BuildBuffers); ++role){
            const Core::GpuGraphResourceId resource = resources[index].*s_GraphResources[role];
            EXPECT_TRUE(view.validResource(resource));
            EXPECT_EQ(resource.generation, view.generation());
            EXPECT_EQ(view.bufferForResource(resource), (context.builds[index].*s_BuildBuffers[role]).get());
        }
    }
}


TEST(PreparedSoftwareBvhGraphResources, EmptyBuildsClearReusedOutputAndSucceed){
    BuildContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/prepared_sw_bvh/empty"));
    PreparedMeshSwBvhGraphResourceVector resources(scratch);
    resources.emplace_back();
    EXPECT_TRUE(ResolvePreparedSoftwareBvhGraphResources(context.graph, context.builds, resources));
    EXPECT_TRUE(resources.empty());
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.resourceCount(), 0u);
}

TEST(PreparedSoftwareBvhGraphResources, ResolvesAllRolesAndPreservesRepeatedBuildsAndCompleteSnapshots){
    BuildContext context;
    for(usize index = 0u; index < 11u; ++index)
        context.addBuffer();
    context.addBuild(7u, 4u, 9u);
    context.addBuild(0u, 4u, 3u);
    context.builds.push_back(context.builds.front());
    Core::Alloc::ScratchArena scratch(Name("tests/prepared_sw_bvh/order"));
    PreparedMeshSwBvhGraphResourceVector resources(scratch);
    resources.reserve(context.builds.size());
    ASSERT_TRUE(ResolvePreparedSoftwareBvhGraphResources(context.graph, context.builds, resources));
    ASSERT_NO_FATAL_FAILURE(ExpectRows(context, resources));
    EXPECT_EQ(resources[0u].position, resources[2u].position);
    EXPECT_NE(resources[0u].position, resources[1u].position);
    EXPECT_EQ(resources[0u].sortKeys, resources[1u].sortKeys);
    EXPECT_EQ(resources[0u].sortPayload, resources[1u].sortPayload);
    EXPECT_EQ(resources[0u].visitCounter, resources[1u].visitCounter);
}

TEST(PreparedSoftwareBvhGraphResources, AnyMissingRoleDiscardsEveryPreparedRowWithoutImportingOrRetainingFailures){
    for(usize role = 0u; role < LengthOf(s_BuildBuffers); ++role){
        for(const bool useNull : { false, true }){
            BuildContext context;
            for(usize index = 0u; index < 7u; ++index)
                context.addBuffer();
            for(usize index = 0u; index < 3u; ++index)
                context.addBuild(0u, 4u, index);
            Core::Alloc::ScratchArena scratch(Name("tests/prepared_sw_bvh/missing"));
            PreparedMeshSwBvhGraphResourceVector resources(scratch);
            resources.reserve(context.builds.size());
            ASSERT_TRUE(ResolvePreparedSoftwareBvhGraphResources(context.graph, context.builds, resources));
            const Core::BufferHandle missing = useNull
                ? Core::BufferHandle{}
                : context.makeBuffer(Name("tests/prepared_sw_bvh/unimported"))
            ;
            context.builds[1u].*s_BuildBuffers[role] = missing;
            EXPECT_FALSE(ResolvePreparedSoftwareBvhGraphResources(context.graph, context.builds, resources));
            EXPECT_TRUE(resources.empty());
            if(missing)
                EXPECT_EQ(missing->getReferenceCount(), 2u);
            const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
            ASSERT_TRUE(view.valid());
            EXPECT_EQ(view.resourceCount(), 7u);
            EXPECT_FALSE(view.findImportedBuffer(missing).valid());
        }
    }
}

TEST(PreparedSoftwareBvhGraphResources, FreshCallsObserveLateImportsCurrentHandlesAndGraphGeneration){
    BuildContext context;
    for(usize index = 0u; index < 7u; ++index)
        context.addBuffer(index != 3u);
    context.addBuild(0u, 4u, 1u);
    Core::Alloc::ScratchArena scratch(Name("tests/prepared_sw_bvh/current_graph"));
    PreparedMeshSwBvhGraphResourceVector resources(scratch);
    resources.reserve(1u);
    EXPECT_FALSE(ResolvePreparedSoftwareBvhGraphResources(context.graph, context.builds, resources));
    EXPECT_TRUE(resources.empty());
    const auto& parent = context.buffers[3u];
    ASSERT_TRUE(context.importBuffer(parent, parent->getCreationDescription().debugName).valid());
    ASSERT_TRUE(ResolvePreparedSoftwareBvhGraphResources(context.graph, context.builds, resources));
    const auto previousParent = resources[0u].parent;
    context.graph.reset();
    for(usize remaining = context.buffers.size(); remaining != 0u; --remaining){
        const auto& buffer = context.buffers[remaining - 1u];
        ASSERT_TRUE(context.importBuffer(buffer, buffer->getCreationDescription().debugName).valid());
    }
    ASSERT_TRUE(ResolvePreparedSoftwareBvhGraphResources(context.graph, context.builds, resources));
    ASSERT_NO_FATAL_FAILURE(ExpectRows(context, resources));
    EXPECT_NE(resources[0u].parent.generation, previousParent.generation);
    const auto replacement = context.makeBuffer(Name("tests/prepared_sw_bvh/replacement"));
    context.builds[0u].nodeBuffer = replacement;
    EXPECT_FALSE(ResolvePreparedSoftwareBvhGraphResources(context.graph, context.builds, resources));
    EXPECT_TRUE(resources.empty());
    const auto replacementId = context.importBuffer(replacement, replacement->getCreationDescription().debugName);
    ASSERT_TRUE(replacementId.valid());
    ASSERT_TRUE(ResolvePreparedSoftwareBvhGraphResources(context.graph, context.builds, resources));
    EXPECT_EQ(resources[0u].node, replacementId);
    ASSERT_NO_FATAL_FAILURE(ExpectRows(context, resources));
}

TEST(PreparedSoftwareBvhGraphResources, ExistingPointerAliasesBypassUnrelatedBuildAndResourceMetadataValidation){
    BuildContext context;
    context.buffers.push_back(context.makeBuffer(NAME_NONE));
    for(usize index = 1u; index < 7u; ++index)
        context.addBuffer();
    const auto alias = context.importBuffer(context.buffers[0u], Name("tests/prepared_sw_bvh/unnamed_alias"));
    ASSERT_TRUE(alias.valid());
    context.addBuild(0u, 4u, 1u);
    context.builds[0u].positionHeapHandle = {};
    context.builds[0u].positionByteSize = 0u;
    Core::BufferDesc& description = const_cast<Core::BufferDesc&>(context.buffers[0u]->getDescription());
    ++description.byteSize;
    ASSERT_FALSE(context.buffers[0u]->descriptionMatchesCreation());
    Core::Alloc::ScratchArena scratch(Name("tests/prepared_sw_bvh/metadata"));
    PreparedMeshSwBvhGraphResourceVector resources(scratch);
    resources.reserve(1u);
    ASSERT_TRUE(ResolvePreparedSoftwareBvhGraphResources(context.graph, context.builds, resources));
    EXPECT_EQ(resources[0u].position, alias);
    ASSERT_NO_FATAL_FAILURE(ExpectRows(context, resources));
    description = context.buffers[0u]->getCreationDescription();
}

TEST(PreparedSoftwareBvhGraphResources, ExactResourceAliasesRemainPresentInEveryRoleAndOwnEverySnapshotHandle){
    BuildContext context;
    for(usize index = 0u; index < 7u; ++index)
        context.addBuffer();
    context.addBuild(0u, 4u, 1u);
    for(const BuildBufferMember member : s_BuildBuffers)
        context.builds[0u].*member = context.buffers[0u];
    const auto referencesBefore = context.buffers[0u]->getReferenceCount();
    Core::Alloc::ScratchArena scratch(Name("tests/prepared_sw_bvh/owning_aliases"));
    PreparedMeshSwBvhGraphResourceVector resources(scratch);
    resources.reserve(1u);
    ASSERT_TRUE(ResolvePreparedSoftwareBvhGraphResources(context.graph, context.builds, resources));
    ASSERT_NO_FATAL_FAILURE(ExpectRows(context, resources));
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), referencesBefore + LengthOf(s_BuildBuffers));
    for(const GraphResourceMember member : s_GraphResources)
        EXPECT_EQ(resources[0u].*member, resources[0u].position);
    context.builds.clear();
    context.graph.reset();
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 1u + LengthOf(s_BuildBuffers));
    resources.clear();
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 1u);
}

TEST(PreparedSoftwareBvhGraphResources, LargePermutedRequestsRetainOrderAndLateFallbackDiscardsTheWholeResult){
    BuildContext context;
    constexpr usize s_BuildCount = 48u;
    constexpr usize s_UnrelatedCount = 16u;
    const usize sharedFirst = s_UnrelatedCount;
    const usize privateFirst = sharedFirst + 3u;
    for(usize index = 0u; index < privateFirst + s_BuildCount * 4u; ++index)
        context.addBuffer();
    for(usize index = 0u; index < s_BuildCount; ++index)
        context.addBuild(privateFirst + ((index * 17u) % s_BuildCount) * 4u, sharedFirst, index);
    Core::Alloc::ScratchArena scratch(Name("tests/prepared_sw_bvh/large"));
    PreparedMeshSwBvhGraphResourceVector resources(scratch);
    resources.reserve(s_BuildCount);
    ASSERT_TRUE(ResolvePreparedSoftwareBvhGraphResources(context.graph, context.builds, resources));
    ASSERT_NO_FATAL_FAILURE(ExpectRows(context, resources));
    const auto previousPosition = resources.front().position;
    context.graph.reset();
    for(usize remaining = context.buffers.size(); remaining != 0u; --remaining){
        const auto& buffer = context.buffers[remaining - 1u];
        ASSERT_TRUE(context.importBuffer(buffer, buffer->getCreationDescription().debugName).valid());
    }
    ASSERT_TRUE(ResolvePreparedSoftwareBvhGraphResources(context.graph, context.builds, resources));
    ASSERT_NO_FATAL_FAILURE(ExpectRows(context, resources));
    EXPECT_NE(resources.front().position.generation, previousPosition.generation);
    const auto missing = context.makeBuffer(Name("tests/prepared_sw_bvh/large_missing"));
    context.builds.back().visitCounterBuffer = missing;
    EXPECT_FALSE(ResolvePreparedSoftwareBvhGraphResources(context.graph, context.builds, resources));
    EXPECT_TRUE(resources.empty());
    EXPECT_EQ(missing->getReferenceCount(), 2u);
}

TEST(PreparedSoftwareBvhGraphResources, UniqueAndRepeatedRequestsDoNotAllocateBeyondReservedOutput){
    struct Workload{
        usize uniqueMeshes;
        usize buildCount;
    };
    constexpr Workload s_Workloads[]{ { 1u, 1u }, { 1u, 128u }, { 7u, 7u }, { 40u, 40u }, { 40u, 128u } };
    for(const Workload& workload : s_Workloads){
        BuildContext context;
        for(usize index = 0u; index < workload.uniqueMeshes * 4u + 3u; ++index)
            context.addBuffer();
        for(usize index = 0u; index < workload.buildCount; ++index)
            context.addBuild(3u + (index % workload.uniqueMeshes) * 4u, 0u, index);
        Core::Alloc::ScratchArena scratch(Name("tests/prepared_sw_bvh/small_storage"));
        PreparedMeshSwBvhGraphResourceVector resources(scratch);
        resources.reserve(workload.buildCount);
        const ArenaMemoryStats before = scratch.memoryStats();
        ASSERT_TRUE(ResolvePreparedSoftwareBvhGraphResources(context.graph, context.builds, resources));
        const ArenaMemoryStats after = scratch.memoryStats();
        EXPECT_EQ(after.allocationCount, before.allocationCount);
        EXPECT_EQ(after.usedBytes, before.usedBytes);
        EXPECT_EQ(after.peakUsedBytes, before.peakUsedBytes);
        EXPECT_EQ(after.reservedBytes, before.reservedBytes);
        ASSERT_NO_FATAL_FAILURE(ExpectRows(context, resources));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

static void BenchmarkGraphResources(
    const usize uniqueMeshes,
    const usize buildCount,
    const usize unrelatedCount,
    const usize iterations,
    const bool lateMissing = false){
    BuildContext context;
    const usize sharedFirst = unrelatedCount;
    const usize privateFirst = sharedFirst + 3u;
    const usize bufferCount = privateFirst + uniqueMeshes * 4u;
    context.buffers.reserve(bufferCount);
    context.builds.reserve(buildCount);
    for(usize index = 0u; index < bufferCount; ++index)
        context.addBuffer();
    for(usize index = 0u; index < buildCount; ++index)
        context.addBuild(privateFirst + (index % uniqueMeshes) * 4u, sharedFirst, index);
    Core::BufferHandle missing;
    if(lateMissing){
        missing = context.makeBuffer(Name("tests/prepared_sw_bvh/benchmark_missing"));
        context.builds.back().visitCounterBuffer = missing;
    }
    u64 elapsed = 0u;
    u64 scratchPeak = 0u;
    u64 scratchExtraPeak = 0u;
    u64 scratchReserved = 0u;
    usize matchingOutcomes = 0u;
    usize totalRows = 0u;
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        Core::Alloc::ScratchArena scratch(Name("tests/prepared_sw_bvh/benchmark_scratch"));
        PreparedMeshSwBvhGraphResourceVector resources(scratch);
        // The renderer reserves these rows before the late graph lookup phase. Keep that allocation outside the
        // lookup timer too, while the exact helper's owning build copies and fallback cleanup remain measured.
        resources.reserve(buildCount);
        const ArenaMemoryStats before = scratch.memoryStats();
        const Timer begin = TimerNow();
        const bool resolved = ResolvePreparedSoftwareBvhGraphResources(context.graph, context.builds, resources);
        elapsed += DurationInNS<u64>(TimerNow(), begin);
        matchingOutcomes += resolved != lateMissing ? 1u : 0u;
        totalRows += resources.size();
        const ArenaMemoryStats after = scratch.memoryStats();
        scratchPeak = Max(scratchPeak, after.peakUsedBytes);
        scratchExtraPeak = Max(scratchExtraPeak, after.peakUsedBytes - before.peakUsedBytes);
        scratchReserved = Max(scratchReserved, after.reservedBytes);
        if(iteration == 0u && resolved)
            ASSERT_NO_FATAL_FAILURE(ExpectRows(context, resources));
    }
    EXPECT_EQ(matchingOutcomes, iterations);
    EXPECT_EQ(totalRows, lateMissing ? 0u : buildCount * iterations);
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.resourceCount(), bufferCount);
    if(lateMissing){
        EXPECT_FALSE(view.findImportedBuffer(missing).valid());
        EXPECT_EQ(missing->getReferenceCount(), 2u);
    }
    RecordUnsignedProperty(MakeNotNull("sw_bvh_resource_lookup_ns"), elapsed);
    RecordUnsignedProperty(MakeNotNull("sw_bvh_resource_lookup_iterations"), iterations);
    RecordUnsignedProperty(MakeNotNull("sw_bvh_resource_lookup_unique_meshes"), uniqueMeshes);
    RecordUnsignedProperty(MakeNotNull("sw_bvh_resource_lookup_builds"), buildCount);
    RecordUnsignedProperty(MakeNotNull("sw_bvh_resource_lookup_unrelated"), unrelatedCount);
    RecordUnsignedProperty(MakeNotNull("sw_bvh_resource_lookup_late_missing"), lateMissing ? 1u : 0u);
    RecordUnsignedProperty(MakeNotNull("sw_bvh_resource_lookup_scratch_peak_bytes"), scratchPeak);
    RecordUnsignedProperty(MakeNotNull("sw_bvh_resource_lookup_extra_peak_bytes"), scratchExtraPeak);
    RecordUnsignedProperty(MakeNotNull("sw_bvh_resource_lookup_scratch_reserved_bytes"), scratchReserved);
}

TEST(PreparedSoftwareBvhGraphResourcesBenchmark, DISABLED_Unique1){
    BenchmarkGraphResources(1u, 1u, 0u, 1024u);
}

TEST(PreparedSoftwareBvhGraphResourcesBenchmark, DISABLED_Unique7){
    BenchmarkGraphResources(7u, 7u, 0u, 256u);
}

TEST(PreparedSoftwareBvhGraphResourcesBenchmark, DISABLED_Unique8){
    BenchmarkGraphResources(8u, 8u, 0u, 256u);
}

TEST(PreparedSoftwareBvhGraphResourcesBenchmark, DISABLED_Unique32){
    BenchmarkGraphResources(32u, 32u, 0u, 64u);
}

TEST(PreparedSoftwareBvhGraphResourcesBenchmark, DISABLED_Unique1024WithUnrelated1024){
    BenchmarkGraphResources(1024u, 1024u, 1024u, 1u);
}

TEST(PreparedSoftwareBvhGraphResourcesBenchmark, DISABLED_Repeated4096){
    BenchmarkGraphResources(1u, 4096u, 0u, 1u);
}

TEST(PreparedSoftwareBvhGraphResourcesBenchmark, DISABLED_Sparse1In4096){
    BenchmarkGraphResources(1u, 1u, 4096u, 16u);
}

TEST(PreparedSoftwareBvhGraphResourcesBenchmark, DISABLED_LateMissing1024){
    BenchmarkGraphResources(1024u, 1024u, 1024u, 1u, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

