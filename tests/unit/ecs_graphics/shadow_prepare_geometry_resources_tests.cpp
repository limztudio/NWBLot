// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/shadow_prepare_geometry_resources.h>

#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shadow_prepare_geometry_resources_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Core = NWB::Core;
namespace Impl = NWB::Impl;
using TestArena = NWB::Tests::TestArena<struct ShadowPrepareGeometryResourcesTestsTag>;
using ResourceVector = Vector<Core::GpuGraphResourceId, Core::Alloc::GlobalArena>;

struct GeometryContext{
    TestArena testArena;
    Core::GraphicsAllocator graphicsAllocator{ testArena.arena };
    Core::Alloc::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    Core::GpuTaskGraph graph{ testArena.arena };
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> buffers{ testArena.arena };
    ResourceVector imported{ testArena.arena };
    ResourceVector trace{ testArena.arena };
    Impl::PreparedMeshBlasBuildVector blasBuilds{ testArena.arena };
    Impl::PreparedMeshSwBvhBuildVector softwareBuilds{ testArena.arena };
    Vector<Name, Core::Alloc::GlobalArena> liveMeshNames{ testArena.arena };

    [[nodiscard]] Core::BufferHandle makeBuffer(const Name& identity){
        Core::Buffer* const buffer = NWB::Tests::NewMetadataOnlyBuffer(
            testArena.arena, context, allocator, Core::BufferDesc{}.setByteSize(256u).setDebugName(identity)
        );
        return Core::BufferHandle(buffer, Core::BufferHandle::deleter_type(&testArena.arena), AdoptRef);
    }

    [[nodiscard]] usize addBuffer(const bool includeInTrace = true){
        const usize index = buffers.size();
        char indexText[32u] = {};
        const Name identity = DeriveName(Name("tests/shadow_prepare_geometry/buffer"), FormatDecimal(index, indexText));
        buffers.push_back(makeBuffer(identity));
        const Core::GpuGraphResourceId resource = graph.importBuffer(
            buffers.back(),
            Core::GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel("Shadow Prepare Geometry Buffer")
                .setType(Core::GpuGraphResourceType::Buffer)
                .setInitialState(Core::ResourceStates::Common)
        );
        EXPECT_TRUE(resource.valid());
        imported.push_back(resource);
        if(includeInTrace)
            trace.push_back(resource);
        return index;
    }

    void addBuild(const usize positionIndex, const usize triangleIndex){
        char indexText[32u] = {};
        const Name identity = DeriveName(Name("tests/shadow_prepare_geometry/mesh"), FormatDecimal(blasBuilds.size(), indexText));
        Impl::PreparedMeshBlasBuild blasBuild{};
        blasBuild.meshName = identity;
        blasBuild.positionBuffer = buffers[positionIndex];
        blasBuild.triangleIndexBuffer = buffers[triangleIndex];
        blasBuilds.push_back(Move(blasBuild));
        Impl::PreparedMeshSwBvhBuild softwareBuild{};
        softwareBuild.meshName = identity;
        softwareBuild.positionBuffer = buffers[positionIndex];
        softwareBuild.triangleIndexBuffer = buffers[triangleIndex];
        softwareBuilds.push_back(Move(softwareBuild));
        liveMeshNames.push_back(identity);
    }

    [[nodiscard]] Impl::ShadowPrepareGeometryInputs inputs(const bool blasBuildsGraphOwned = true)const{
        return Impl::ShadowPrepareGeometryInputs{
            .blasBuilds = blasBuilds,
            .softwareBuilds = softwareBuilds,
            .traceResources = trace.data(),
            .traceResourceCount = trace.size(),
            .blasBuildsGraphOwned = blasBuildsGraphOwned,
        };
    }
};


TEST(ShadowPrepareGeometryResources, KeepsFirstBuildOrderAndTraceMultiplicityAcrossImportPhases){
    GeometryContext context;
    for(usize index = 0u; index < 6u; ++index)
        ASSERT_EQ(context.addBuffer(), index);
    context.addBuild(3u, 1u);
    context.addBuild(1u, 4u);
    context.addBuild(3u, 1u);
    context.softwareBuilds[0u].positionBuffer = context.buffers[4u];
    context.softwareBuilds[0u].triangleIndexBuffer = context.buffers[0u];
    context.softwareBuilds[1u].positionBuffer = context.buffers[3u];
    context.softwareBuilds[1u].triangleIndexBuffer = context.buffers[4u];
    context.softwareBuilds.resize(2u);
    context.trace = {
        context.imported[2u], context.imported[4u], context.imported[3u],
        context.imported[1u], context.imported[2u], context.imported[0u],
    };
    Core::Alloc::ScratchArena scratch(Name("tests/shadow_prepare_geometry/order_scratch"));
    Impl::ShadowPrepareGeometryResources selection(context.inputs(), scratch);
    bool blasOwned = true;
    bool softwareOwned = true;
    const auto referencesBefore = context.buffers[3u]->getReferenceCount();
    selection.prepareStorage(blasOwned, softwareOwned);
    selection.gatherBuildInputs(context.graph, blasOwned, softwareOwned);
    EXPECT_TRUE(blasOwned);
    EXPECT_TRUE(softwareOwned);
    ASSERT_EQ(selection.m_blasBuildInputs.size(), 3u);
    EXPECT_EQ(selection.m_blasBuildInputs[0u], context.imported[3u]);
    EXPECT_EQ(selection.m_blasBuildInputs[1u], context.imported[1u]);
    EXPECT_EQ(selection.m_blasBuildInputs[2u], context.imported[4u]);
    ASSERT_EQ(selection.m_softwareTailInputs.size(), 3u);
    EXPECT_EQ(selection.m_softwareTailInputs[0u], context.imported[4u]);
    EXPECT_EQ(selection.m_softwareTailInputs[1u], context.imported[0u]);
    EXPECT_EQ(selection.m_softwareTailInputs[2u], context.imported[3u]);
    EXPECT_EQ(context.buffers[3u]->getReferenceCount(), referencesBefore);

    // These are real graph mutations at the same phase boundary as the renderer. A retained read claim would fail.
    const Core::GpuGraphResourceSetId inputSet = context.graph.importResourceSet(
        Core::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/shadow_prepare_geometry/blas_inputs"))
            .setMarkerLabel("Shadow Prepare Geometry BLAS Inputs")
            .setMembers(selection.m_blasBuildInputs.data(), selection.m_blasBuildInputs.size())
    );
    ASSERT_TRUE(inputSet.valid());
    ASSERT_EQ(context.addBuffer(false), 6u);
    ASSERT_TRUE(selection.gatherRemainingTraceResources());
    ASSERT_EQ(selection.m_remainingTraceGeometry.size(), 3u);
    EXPECT_EQ(selection.m_remainingTraceGeometry[0u], context.imported[2u]);
    EXPECT_EQ(selection.m_remainingTraceGeometry[1u], context.imported[2u]);
    EXPECT_EQ(selection.m_remainingTraceGeometry[2u], context.imported[0u]);
    for(const Impl::PreparedMeshBlasBuild& build : context.blasBuilds)
        EXPECT_TRUE(selection.isPreparedMeshBlasBuild(build.meshName));
    EXPECT_FALSE(selection.isPreparedMeshBlasBuild(Name("tests/shadow_prepare_geometry/not_prepared")));
}

TEST(ShadowPrepareGeometryResources, MissingOrUnlistedBlasInputKeepsBothPoliciesNative){
    for(u32 failure = 0u; failure < 3u; ++failure){
        GeometryContext context;
        ASSERT_EQ(context.addBuffer(), 0u);
        ASSERT_EQ(context.addBuffer(), 1u);
        ASSERT_EQ(context.addBuffer(false), 2u);
        context.addBuild(0u, 1u);
        context.addBuild(1u, 2u);
        if(failure == 0u)
            context.blasBuilds.back().triangleIndexBuffer = nullptr;
        else if(failure == 1u)
            context.blasBuilds.back().triangleIndexBuffer = context.makeBuffer(Name("tests/shadow_prepare_geometry/unimported"));
        Core::Alloc::ScratchArena scratch(Name("tests/shadow_prepare_geometry/blas_fallback_scratch"));
        Impl::ShadowPrepareGeometryResources selection(context.inputs(), scratch);
        bool blasOwned = true;
        bool softwareOwned = true;
        selection.prepareStorage(blasOwned, softwareOwned);
        selection.gatherBuildInputs(context.graph, blasOwned, softwareOwned);
        EXPECT_FALSE(blasOwned);
        EXPECT_FALSE(softwareOwned);
        EXPECT_TRUE(selection.m_blasBuildInputs.empty());
        EXPECT_TRUE(selection.m_softwareTailInputs.empty());
        ASSERT_TRUE(selection.gatherRemainingTraceResources());
        ASSERT_EQ(selection.m_remainingTraceGeometry.size(), context.trace.size());
        for(usize index = 0u; index < context.trace.size(); ++index)
            EXPECT_EQ(selection.m_remainingTraceGeometry[index], context.trace[index]);
    }
}

TEST(ShadowPrepareGeometryResources, LateSoftwareFailureClearsBothListsWithoutDroppingPreparedBlasIdentity){
    GeometryContext context;
    for(usize index = 0u; index < 4u; ++index)
        ASSERT_EQ(context.addBuffer(), index);
    context.addBuild(0u, 1u);
    context.addBuild(2u, 3u);
    context.softwareBuilds.back().triangleIndexBuffer = nullptr;
    Core::Alloc::ScratchArena scratch(Name("tests/shadow_prepare_geometry/software_fallback_scratch"));
    Impl::ShadowPrepareGeometryResources selection(context.inputs(), scratch);
    bool blasOwned = true;
    bool softwareOwned = true;
    selection.prepareStorage(blasOwned, softwareOwned);
    selection.gatherBuildInputs(context.graph, blasOwned, softwareOwned);
    EXPECT_FALSE(blasOwned);
    EXPECT_FALSE(softwareOwned);
    EXPECT_TRUE(selection.m_blasBuildInputs.empty());
    EXPECT_TRUE(selection.m_softwareTailInputs.empty());
    EXPECT_TRUE(selection.isPreparedMeshBlasBuild(context.blasBuilds.back().meshName));
    ASSERT_TRUE(selection.gatherRemainingTraceResources());
    EXPECT_EQ(selection.m_remainingTraceGeometry.size(), context.trace.size());
}

TEST(ShadowPrepareGeometryResources, InactiveLanesSkipTheirInvalidInputsAndEmptyLanesPreservePolicies){
    GeometryContext context;
    ASSERT_EQ(context.addBuffer(), 0u);
    context.addBuild(0u, 0u);
    context.blasBuilds[0u].positionBuffer = nullptr;
    context.softwareBuilds[0u].positionBuffer = nullptr;
    Core::Alloc::ScratchArena scratch(Name("tests/shadow_prepare_geometry/inactive_scratch"));
    {
        Impl::ShadowPrepareGeometryResources selection(context.inputs(false), scratch);
        bool blasOwned = false;
        bool softwareOwned = false;
        selection.prepareStorage(blasOwned, softwareOwned);
        selection.gatherBuildInputs(context.graph, blasOwned, softwareOwned);
        EXPECT_FALSE(blasOwned);
        EXPECT_FALSE(softwareOwned);
        EXPECT_FALSE(selection.isPreparedMeshBlasBuild(context.blasBuilds[0u].meshName));
        ASSERT_TRUE(selection.gatherRemainingTraceResources());
        ASSERT_EQ(selection.m_remainingTraceGeometry.size(), 1u);
        EXPECT_EQ(selection.m_remainingTraceGeometry[0u], context.imported[0u]);
    }
    context.blasBuilds.clear();
    context.softwareBuilds.clear();
    context.trace.clear();
    Impl::ShadowPrepareGeometryResources empty(context.inputs(), scratch);
    bool blasOwned = true;
    bool softwareOwned = true;
    empty.prepareStorage(blasOwned, softwareOwned);
    empty.gatherBuildInputs(context.graph, blasOwned, softwareOwned);
    EXPECT_TRUE(blasOwned);
    EXPECT_TRUE(softwareOwned);
    EXPECT_TRUE(empty.m_blasBuildInputs.empty());
    EXPECT_TRUE(empty.m_softwareTailInputs.empty());
    EXPECT_TRUE(empty.gatherRemainingTraceResources());
    EXPECT_TRUE(empty.m_remainingTraceGeometry.empty());
}

TEST(ShadowPrepareGeometryResources, ComparesEveryNameLaneAndTheFullResourceGeneration){
    GeometryContext context;
    ASSERT_EQ(context.addBuffer(), 0u);
    ASSERT_EQ(context.addBuffer(), 1u);
    context.addBuild(0u, 1u);
    const Name base = context.blasBuilds[0u].meshName;
    for(usize index = 0u; index < 40u; ++index)
        context.addBuild(0u, 1u);
    for(u32 lane = 0u; lane < s_NameHashLaneCount; ++lane){
        NameHash hash = base.identityHash();
        hash.qwords[lane] ^= 1u;
        context.addBuild(0u, 1u);
        context.blasBuilds.back().meshName = Name(hash);
    }
    Core::GpuGraphResourceId stale = context.imported[0u];
    ++stale.generation;
    context.trace[0u] = stale;
    Core::Alloc::ScratchArena scratch(Name("tests/shadow_prepare_geometry/identity_scratch"));
    Impl::ShadowPrepareGeometryResources selection(context.inputs(), scratch);
    for(const Impl::PreparedMeshBlasBuild& build : context.blasBuilds)
        EXPECT_TRUE(selection.isPreparedMeshBlasBuild(build.meshName));
    for(u32 lane = 0u; lane < s_NameHashLaneCount; ++lane){
        NameHash hash = base.identityHash();
        hash.qwords[lane] ^= 2u;
        EXPECT_FALSE(selection.isPreparedMeshBlasBuild(Name(hash)));
    }
    bool blasOwned = true;
    bool softwareOwned = true;
    selection.prepareStorage(blasOwned, softwareOwned);
    selection.gatherBuildInputs(context.graph, blasOwned, softwareOwned);
    EXPECT_FALSE(blasOwned);
    EXPECT_FALSE(softwareOwned);
    EXPECT_TRUE(selection.m_blasBuildInputs.empty());
    // Partitioning validates the opaque ID shape; the graph's later resource-set import owns generation admission.
    ASSERT_TRUE(selection.gatherRemainingTraceResources());
    ASSERT_EQ(selection.m_remainingTraceGeometry.size(), 2u);
    EXPECT_EQ(selection.m_remainingTraceGeometry[0u], stale);
    EXPECT_EQ(selection.m_remainingTraceGeometry[1u], context.imported[1u]);
}

TEST(ShadowPrepareGeometryResources, InvalidTraceEntryRejectsOnlyAtThePartitionPhase){
    GeometryContext context;
    ASSERT_EQ(context.addBuffer(), 0u);
    ASSERT_EQ(context.addBuffer(), 1u);
    ASSERT_EQ(context.addBuffer(), 2u);
    context.addBuild(0u, 1u);
    context.trace.push_back({});
    Core::Alloc::ScratchArena scratch(Name("tests/shadow_prepare_geometry/invalid_trace_scratch"));
    Impl::ShadowPrepareGeometryResources selection(context.inputs(), scratch);
    bool blasOwned = true;
    bool softwareOwned = true;
    selection.prepareStorage(blasOwned, softwareOwned);
    selection.gatherBuildInputs(context.graph, blasOwned, softwareOwned);
    EXPECT_TRUE(blasOwned);
    EXPECT_TRUE(softwareOwned);
    ASSERT_EQ(selection.m_blasBuildInputs.size(), 2u);
    ASSERT_EQ(context.addBuffer(false), 3u);
    EXPECT_FALSE(selection.gatherRemainingTraceResources());
    ASSERT_EQ(selection.m_remainingTraceGeometry.size(), 1u);
    EXPECT_EQ(selection.m_remainingTraceGeometry[0u], context.imported[2u]);
}

TEST(ShadowPrepareGeometryResources, NewOperationUsesReplacedBuffersAndTheCurrentGraphGeneration){
    GeometryContext context;
    ASSERT_EQ(context.addBuffer(), 0u);
    ASSERT_EQ(context.addBuffer(), 1u);
    context.addBuild(0u, 1u);
    const Core::GpuGraphResourceId oldResource = context.imported[0u];
    Core::Alloc::ScratchArena scratch(Name("tests/shadow_prepare_geometry/replacement_scratch"));
    {
        Impl::ShadowPrepareGeometryResources selection(context.inputs(), scratch);
        bool blasOwned = true;
        bool softwareOwned = true;
        selection.prepareStorage(blasOwned, softwareOwned);
        selection.gatherBuildInputs(context.graph, blasOwned, softwareOwned);
        ASSERT_TRUE(blasOwned);
        ASSERT_TRUE(softwareOwned);
        ASSERT_TRUE(selection.gatherRemainingTraceResources());
        EXPECT_TRUE(selection.m_remainingTraceGeometry.empty());
    }
    ASSERT_TRUE(context.graph.tryReset());
    context.trace.clear();
    ASSERT_EQ(context.addBuffer(), 2u);
    ASSERT_EQ(context.addBuffer(), 3u);
    context.blasBuilds[0u].positionBuffer = context.buffers[2u];
    context.blasBuilds[0u].triangleIndexBuffer = context.buffers[3u];
    context.softwareBuilds[0u].positionBuffer = context.buffers[3u];
    context.softwareBuilds[0u].triangleIndexBuffer = context.buffers[2u];
    Impl::ShadowPrepareGeometryResources selection(context.inputs(), scratch);
    bool blasOwned = true;
    bool softwareOwned = true;
    selection.prepareStorage(blasOwned, softwareOwned);
    selection.gatherBuildInputs(context.graph, blasOwned, softwareOwned);
    ASSERT_TRUE(blasOwned);
    ASSERT_TRUE(softwareOwned);
    ASSERT_EQ(selection.m_blasBuildInputs.size(), 2u);
    EXPECT_EQ(selection.m_blasBuildInputs[0u], context.imported[2u]);
    EXPECT_EQ(selection.m_blasBuildInputs[1u], context.imported[3u]);
    EXPECT_NE(selection.m_blasBuildInputs[0u].generation, oldResource.generation);
    ASSERT_EQ(selection.m_softwareTailInputs.size(), 2u);
    EXPECT_EQ(selection.m_softwareTailInputs[0u], context.imported[3u]);
    EXPECT_EQ(selection.m_softwareTailInputs[1u], context.imported[2u]);
    EXPECT_TRUE(selection.gatherRemainingTraceResources());
    EXPECT_TRUE(selection.m_remainingTraceGeometry.empty());
}


TEST(ShadowPrepareGeometryResources, IndexedRequestsPreserveOrderAndClearMembershipOnLateSoftwareFailure){
    GeometryContext context;
    for(usize index = 0u; index < 80u; ++index)
        ASSERT_EQ(context.addBuffer(), index);
    for(usize index = 0u; index < 40u; ++index)
        context.addBuild(index * 2u, index * 2u + 1u);
    for(usize index = 0u; index < 40u; ++index){
        context.softwareBuilds[index].positionBuffer = context.buffers[79u - index * 2u];
        context.softwareBuilds[index].triangleIndexBuffer = context.buffers[78u - index * 2u];
    }
    Core::GpuGraphResourceId stale = context.imported[0u];
    ++stale.generation;
    context.trace.push_back(stale);
    for(u32 failure = 0u; failure < 2u; ++failure){
        if(failure != 0u)
            context.softwareBuilds.back().triangleIndexBuffer = nullptr;
        Core::Alloc::ScratchArena scratch(Name("tests/shadow_prepare_geometry/indexed_scratch"));
        Impl::ShadowPrepareGeometryResources selection(context.inputs(), scratch);
        bool blasOwned = true;
        bool softwareOwned = true;
        selection.prepareStorage(blasOwned, softwareOwned);
        selection.gatherBuildInputs(context.graph, blasOwned, softwareOwned);
        EXPECT_EQ(blasOwned, failure == 0u);
        EXPECT_EQ(softwareOwned, failure == 0u);
        EXPECT_TRUE(selection.isPreparedMeshBlasBuild(context.blasBuilds.back().meshName));
        ASSERT_EQ(context.addBuffer(false), 80u + failure);
        ASSERT_TRUE(selection.gatherRemainingTraceResources());
        if(failure == 0u){
            ASSERT_EQ(selection.m_blasBuildInputs.size(), 80u);
            ASSERT_EQ(selection.m_softwareTailInputs.size(), 80u);
            for(usize index = 0u; index < 80u; ++index){
                EXPECT_EQ(selection.m_blasBuildInputs[index], context.imported[index]);
                EXPECT_EQ(selection.m_softwareTailInputs[index], context.imported[79u - index]);
            }
            ASSERT_EQ(selection.m_remainingTraceGeometry.size(), 1u);
            EXPECT_EQ(selection.m_remainingTraceGeometry[0u], stale);
        }
        else{
            EXPECT_TRUE(selection.m_blasBuildInputs.empty());
            EXPECT_TRUE(selection.m_softwareTailInputs.empty());
            ASSERT_EQ(selection.m_remainingTraceGeometry.size(), context.trace.size());
            for(usize index = 0u; index < context.trace.size(); ++index)
                EXPECT_EQ(selection.m_remainingTraceGeometry[index], context.trace[index]);
        }
    }
}

TEST(ShadowPrepareGeometryResources, ScratchUsageTracksUniqueRequestsInsteadOfBuildOrGraphSize){
    struct Workload{
        usize uniqueMeshes;
        usize builds;
        usize unrelatedBuffers;
    };
    const Workload workloads[] = {
        { 1u, 1u, 0u },
        { 1u, 1024u, 0u },
        { 40u, 40u, 0u },
        { 40u, 40u, 512u },
    };
    u64 peaks[LengthOf(workloads)] = {};
    for(usize workloadIndex = 0u; workloadIndex < LengthOf(workloads); ++workloadIndex){
        const Workload& workload = workloads[workloadIndex];
        GeometryContext context;
        for(usize index = 0u; index < workload.unrelatedBuffers; ++index)
            ASSERT_EQ(context.addBuffer(false), index);
        for(usize index = 0u; index < workload.uniqueMeshes * 3u; ++index)
            ASSERT_EQ(context.addBuffer(), workload.unrelatedBuffers + index);
        for(usize index = 0u; index < workload.builds; ++index){
            const usize firstBuffer = workload.unrelatedBuffers + (index % workload.uniqueMeshes) * 3u;
            context.addBuild(firstBuffer, firstBuffer + 1u);
        }
        Core::Alloc::ScratchArena scratch(Name("tests/shadow_prepare_geometry/storage_scratch"));
        {
            Impl::ShadowPrepareGeometryResources selection(context.inputs(), scratch);
            bool blasOwned = true;
            bool softwareOwned = true;
            selection.prepareStorage(blasOwned, softwareOwned);
            selection.gatherBuildInputs(context.graph, blasOwned, softwareOwned);
            ASSERT_TRUE(blasOwned);
            ASSERT_TRUE(softwareOwned);
            ASSERT_TRUE(selection.gatherRemainingTraceResources());
            EXPECT_EQ(selection.m_blasBuildInputs.size(), workload.uniqueMeshes * 2u);
            EXPECT_EQ(selection.m_softwareTailInputs.size(), workload.uniqueMeshes * 2u);
            EXPECT_EQ(selection.m_remainingTraceGeometry.size(), workload.uniqueMeshes);
        }
        peaks[workloadIndex] = scratch.memoryStats().peakUsedBytes;
    }
    EXPECT_EQ(peaks[1u], peaks[0u]);
    EXPECT_EQ(peaks[3u], peaks[2u]);
}


static void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

static void BenchmarkSelection(
    const usize uniqueMeshCount,
    const usize buildCount,
    const usize unrelatedBufferCount,
    const usize iterations){
    GeometryContext context;
    const usize bufferCount = unrelatedBufferCount + uniqueMeshCount * 3u;
    context.buffers.reserve(bufferCount);
    context.imported.reserve(bufferCount);
    context.trace.reserve(uniqueMeshCount * 3u);
    context.blasBuilds.reserve(buildCount);
    context.softwareBuilds.reserve(buildCount);
    context.liveMeshNames.reserve(buildCount);
    for(usize index = 0u; index < bufferCount; ++index)
        ASSERT_EQ(context.addBuffer(index >= unrelatedBufferCount), index);
    for(usize index = 0u; index < buildCount; ++index){
        const usize firstBuffer = unrelatedBufferCount + (index % uniqueMeshCount) * 3u;
        context.addBuild(firstBuffer, firstBuffer + 1u);
    }
    const Impl::ShadowPrepareGeometryInputs inputs = context.inputs();
    u64 totalNanoseconds = 0u;
    u64 buildNanoseconds = 0u;
    u64 partitionNanoseconds = 0u;
    u64 meshLookupNanoseconds = 0u;
    u64 scratchPeak = 0u;
    u64 scratchReserved = 0u;
    usize totalBlasInputs = 0u;
    usize totalSoftwareInputs = 0u;
    usize totalRemaining = 0u;
    usize totalPrepared = 0u;
    bool succeeded = true;
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        Core::Alloc::ScratchArena scratch(Name("tests/shadow_prepare_geometry/benchmark_scratch"));
        const Timer begin = TimerNow();
        {
            Impl::ShadowPrepareGeometryResources selection(inputs, scratch);
            bool blasOwned = true;
            bool softwareOwned = true;
            selection.prepareStorage(blasOwned, softwareOwned);
            const Timer buildBegin = TimerNow();
            selection.gatherBuildInputs(context.graph, blasOwned, softwareOwned);
            buildNanoseconds += DurationInNS<u64>(TimerNow(), buildBegin);
            const Timer partitionBegin = TimerNow();
            const bool partitioned = selection.gatherRemainingTraceResources();
            partitionNanoseconds += DurationInNS<u64>(TimerNow(), partitionBegin);
            const Timer meshLookupBegin = TimerNow();
            for(const Name& meshName : context.liveMeshNames){
                if(selection.isPreparedMeshBlasBuild(meshName))
                    ++totalPrepared;
            }
            meshLookupNanoseconds += DurationInNS<u64>(TimerNow(), meshLookupBegin);
            succeeded = succeeded && blasOwned && softwareOwned && partitioned;
            totalBlasInputs += selection.m_blasBuildInputs.size();
            totalSoftwareInputs += selection.m_softwareTailInputs.size();
            totalRemaining += selection.m_remainingTraceGeometry.size();
        }
        totalNanoseconds += DurationInNS<u64>(TimerNow(), begin);
        const auto statistics = scratch.memoryStats();
        scratchPeak = Max(scratchPeak, statistics.peakUsedBytes);
        scratchReserved = Max(scratchReserved, statistics.reservedBytes);
    }
    EXPECT_TRUE(succeeded);
    EXPECT_EQ(totalBlasInputs, uniqueMeshCount * 2u * iterations);
    EXPECT_EQ(totalSoftwareInputs, uniqueMeshCount * 2u * iterations);
    EXPECT_EQ(totalRemaining, uniqueMeshCount * iterations);
    EXPECT_EQ(totalPrepared, buildCount * iterations);
    RecordUnsignedProperty(MakeNotNull("shadow_membership_total_ns"), totalNanoseconds);
    RecordUnsignedProperty(MakeNotNull("shadow_membership_build_ns"), buildNanoseconds);
    RecordUnsignedProperty(MakeNotNull("shadow_membership_partition_ns"), partitionNanoseconds);
    RecordUnsignedProperty(MakeNotNull("shadow_membership_mesh_lookup_ns"), meshLookupNanoseconds);
    RecordUnsignedProperty(MakeNotNull("shadow_membership_iterations"), iterations);
    RecordUnsignedProperty(MakeNotNull("shadow_membership_unique_mesh_count"), uniqueMeshCount);
    RecordUnsignedProperty(MakeNotNull("shadow_membership_build_count"), buildCount);
    RecordUnsignedProperty(MakeNotNull("shadow_membership_unrelated_buffer_count"), unrelatedBufferCount);
    RecordUnsignedProperty(MakeNotNull("shadow_membership_scratch_peak_bytes"), scratchPeak);
    RecordUnsignedProperty(MakeNotNull("shadow_membership_scratch_reserved_bytes"), scratchReserved);
}

TEST(ShadowPrepareGeometryResourcesBenchmark, DISABLED_Unique1){
    BenchmarkSelection(1u, 1u, 0u, 1024u);
}

TEST(ShadowPrepareGeometryResourcesBenchmark, DISABLED_Unique8){
    BenchmarkSelection(8u, 8u, 0u, 256u);
}

TEST(ShadowPrepareGeometryResourcesBenchmark, DISABLED_Unique16){
    BenchmarkSelection(16u, 16u, 0u, 256u);
}

TEST(ShadowPrepareGeometryResourcesBenchmark, DISABLED_Unique32){
    BenchmarkSelection(32u, 32u, 0u, 64u);
}

TEST(ShadowPrepareGeometryResourcesBenchmark, DISABLED_Unique64){
    BenchmarkSelection(64u, 64u, 0u, 32u);
}

TEST(ShadowPrepareGeometryResourcesBenchmark, DISABLED_Unique1024){
    BenchmarkSelection(1024u, 1024u, 1024u, 1u);
}

TEST(ShadowPrepareGeometryResourcesBenchmark, DISABLED_Shared4096){
    BenchmarkSelection(1u, 4096u, 0u, 1u);
}

TEST(ShadowPrepareGeometryResourcesBenchmark, DISABLED_Sparse1In4096){
    BenchmarkSelection(1u, 1u, 4096u, 32u);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

