// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/raytrace/shadow_trace_geometry.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shadow_trace_geometry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Core = NWB::Core;
namespace Impl = NWB::Impl;
namespace Roles = Impl::PreparedShadowTraceGeometryRole;
using MeshSnapshot = Impl::ECSRenderDetail::MeshRayTracingResourceSnapshot;
using SelectedBuffers = Vector<Core::Buffer*, Core::Alloc::GlobalArena>;
using TestArena = NWB::Tests::TestArena<struct ShadowTraceGeometryTestsTag>;


struct GeometryContext{
    TestArena testArena;
    Core::GraphicsAllocator graphicsAllocator{ testArena.arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    Core::Alloc::ScratchArena meshArena{ Name("tests/shadow_trace_geometry/meshes") };
    Core::Alloc::ScratchArena freezeArena{ Name("tests/shadow_trace_geometry/freeze") };
    Impl::ECSRenderDetail::MeshRayTracingResourceSnapshotVector meshes{ meshArena };
    SelectedBuffers hardwarePositions{ testArena.arena };
    SelectedBuffers hardwareIndices{ testArena.arena };
    SelectedBuffers hardwareAttributes{ testArena.arena };
    SelectedBuffers softwareNodes{ testArena.arena };
    SelectedBuffers softwarePositions{ testArena.arena };
    SelectedBuffers softwareIndices{ testArena.arena };
    SelectedBuffers softwareAttributes{ testArena.arena };
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> accepted{ testArena.arena };
    Impl::PreparedShadowTraceGeometryBufferVector prepared{ testArena.arena };


    [[nodiscard]] Core::BufferHandle makeBuffer(const Name& identity){
        Core::Buffer* const buffer = NWB::Tests::NewMetadataOnlyBuffer(
            testArena.arena,
            context,
            allocator,
            Core::BufferDesc{}.setByteSize(256u).setDebugName(identity)
        );
        return Core::BufferHandle(buffer, Core::BufferHandle::deleter_type(&testArena.arena), AdoptRef);
    }

    void addMesh(const usize index){
        char indexText[32u] = {};
        MeshSnapshot mesh;
        mesh.meshName = DeriveName(Name("tests/shadow_trace_geometry/mesh"), FormatDecimal(index, indexText));
        mesh.positionBuffer = makeBuffer(DeriveName(mesh.meshName, AStringView(":position")));
        mesh.triangleIndexBuffer = makeBuffer(DeriveName(mesh.meshName, AStringView(":index")));
        mesh.attributeBuffer = makeBuffer(DeriveName(mesh.meshName, AStringView(":attribute")));
        mesh.swBvhNodeBuffer = makeBuffer(DeriveName(mesh.meshName, AStringView(":node")));
        meshes.push_back(Move(mesh));
    }

    [[nodiscard]] bool freeze(const bool hardware, const bool software){
        const Impl::ShadowTraceGeometrySelection selection{
            .hardwarePositions = hardwarePositions,
            .hardwareIndices = hardwareIndices,
            .hardwareAttributes = hardwareAttributes,
            .softwareNodes = softwareNodes,
            .softwarePositions = softwarePositions,
            .softwareIndices = softwareIndices,
            .softwareAttributes = softwareAttributes,
            .includeHardware = hardware,
            .includeSoftware = software,
        };
        return Impl::FreezePreparedShadowTraceGeometryBuffers(meshes, selection, freezeArena, accepted, prepared);
    }
};


TEST(ShadowTraceGeometry, InactiveBackendsPruneRetiredOwnersAndRetainInvisibleMeshes){
    GeometryContext context;
    context.meshes.reserve(2u);
    context.addMesh(0u);
    context.addMesh(1u);
    context.accepted.push_back(context.meshes[0u].positionBuffer);
    context.accepted.push_back(context.meshes[1u].swBvhNodeBuffer);
    context.meshes.pop_back();
    context.softwarePositions.push_back(nullptr);
    ASSERT_TRUE(context.freeze(false, false));
    EXPECT_TRUE(context.prepared.empty());
    ASSERT_EQ(context.accepted.size(), 1u);
    EXPECT_EQ(context.accepted[0u].get(), context.meshes[0u].positionBuffer.get());
}

TEST(ShadowTraceGeometry, PreservesSelectionOrderMemberIdentityAndCombinedRoles){
    GeometryContext context;
    context.meshes.reserve(2u);
    context.addMesh(0u);
    context.addMesh(1u);
    MeshSnapshot& first = context.meshes[0u];
    MeshSnapshot& second = context.meshes[1u];
    first.attributeBuffer = second.positionBuffer;
    context.hardwarePositions.push_back(second.positionBuffer.get());
    context.hardwarePositions.push_back(first.positionBuffer.get());
    context.hardwarePositions.push_back(second.positionBuffer.get());
    context.hardwareIndices.push_back(first.triangleIndexBuffer.get());
    context.hardwareAttributes.push_back(second.positionBuffer.get());
    context.softwareNodes.push_back(first.swBvhNodeBuffer.get());
    context.softwarePositions.push_back(second.positionBuffer.get());
    context.softwareIndices.push_back(first.triangleIndexBuffer.get());
    context.softwareAttributes.push_back(second.positionBuffer.get());

    ASSERT_TRUE(context.freeze(true, true));
    ASSERT_EQ(context.prepared.size(), 4u);
    EXPECT_EQ(context.prepared[0u].buffer.get(), second.positionBuffer.get());
    EXPECT_EQ(context.prepared[1u].buffer.get(), first.positionBuffer.get());
    EXPECT_EQ(context.prepared[2u].buffer.get(), first.triangleIndexBuffer.get());
    EXPECT_EQ(context.prepared[3u].buffer.get(), first.swBvhNodeBuffer.get());
    EXPECT_EQ(context.prepared[0u].identity, DeriveName(second.meshName, AStringView(":shadow_trace_hw_position")));
    EXPECT_EQ(
        context.prepared[0u].roles,
        Roles::HardwarePosition | Roles::HardwareAttribute | Roles::SoftwarePosition | Roles::SoftwareAttribute
    );
    EXPECT_EQ(context.prepared[2u].roles, Roles::HardwareIndex | Roles::SoftwareIndex);

    // The same pointer in another member cannot replace that member's first owning mesh.
    context.hardwarePositions.clear();
    context.hardwareIndices.clear();
    ASSERT_TRUE(context.freeze(true, false));
    ASSERT_EQ(context.prepared.size(), 1u);
    EXPECT_EQ(context.prepared[0u].identity, DeriveName(first.meshName, AStringView(":shadow_trace_hw_attribute")));
    EXPECT_EQ(context.prepared[0u].roles, Roles::HardwareAttribute);

    // Duplicate ownership in the same member resolves to the first mesh in the snapshot order.
    second.attributeBuffer = first.attributeBuffer;
    ASSERT_TRUE(context.freeze(true, false));
    EXPECT_EQ(context.prepared[0u].identity, DeriveName(first.meshName, AStringView(":shadow_trace_hw_attribute")));
}

TEST(ShadowTraceGeometry, IncludesOffscreenPendingAndRuntimeBuildInputs){
    GeometryContext context;
    context.meshes.reserve(3u);
    for(usize meshIndex = 0u; meshIndex < 3u; ++meshIndex)
        context.addMesh(meshIndex);
    context.meshes[0u].runtimeMesh = true;
    context.meshes[1u].blasBuildPending = true;
    context.meshes[2u].swBvhBuildPending = true;
    ASSERT_TRUE(context.freeze(true, true));
    ASSERT_EQ(context.prepared.size(), 8u);
    EXPECT_EQ(context.prepared[0u].buffer.get(), context.meshes[0u].positionBuffer.get());
    EXPECT_EQ(context.prepared[0u].roles, Roles::HardwarePosition | Roles::SoftwarePosition);
    EXPECT_EQ(context.prepared[2u].buffer.get(), context.meshes[1u].positionBuffer.get());
    EXPECT_EQ(context.prepared[4u].buffer.get(), context.meshes[0u].swBvhNodeBuffer.get());
    EXPECT_EQ(context.prepared[5u].buffer.get(), context.meshes[2u].swBvhNodeBuffer.get());
    EXPECT_EQ(context.prepared[7u].buffer.get(), context.meshes[2u].triangleIndexBuffer.get());
    for(const Impl::PreparedShadowTraceGeometryBuffer& prepared : context.prepared){
        EXPECT_TRUE(prepared.normalizationPending);
        EXPECT_EQ(prepared.initialState, prepared.buffer->getCreationDescription().initialState);
    }
    EXPECT_GE(context.accepted.capacity(), context.accepted.size() + context.prepared.size());

    ASSERT_TRUE(context.freeze(false, true));
    ASSERT_EQ(context.prepared.size(), 6u);
    EXPECT_EQ(context.prepared[0u].roles, Roles::SoftwareNode);
    for(const Impl::PreparedShadowTraceGeometryBuffer& prepared : context.prepared)
        EXPECT_EQ(prepared.roles & (Roles::HardwarePosition | Roles::HardwareIndex | Roles::HardwareAttribute), 0u);
}

TEST(ShadowTraceGeometry, PreservesAcceptedStateAcrossGrowthAndResourceReplacement){
    GeometryContext context;
    context.meshes.reserve(33u);
    context.addMesh(0u);
    Core::BufferHandle original = context.meshes[0u].positionBuffer;
    context.accepted.push_back(original);
    context.hardwarePositions.push_back(original.get());
    ASSERT_TRUE(context.freeze(true, false));
    ASSERT_EQ(context.prepared.size(), 1u);
    EXPECT_FALSE(context.prepared[0u].normalizationPending);
    EXPECT_EQ(context.prepared[0u].initialState, Core::ResourceStates::ShaderResource);

    for(usize meshIndex = 1u; meshIndex < 33u; ++meshIndex){
        context.addMesh(meshIndex);
        context.hardwarePositions.push_back(context.meshes.back().positionBuffer.get());
    }
    ASSERT_TRUE(context.freeze(true, false));
    ASSERT_EQ(context.prepared.size(), 33u);
    EXPECT_FALSE(context.prepared[0u].normalizationPending);
    for(usize bufferIndex = 1u; bufferIndex < context.prepared.size(); ++bufferIndex)
        EXPECT_TRUE(context.prepared[bufferIndex].normalizationPending);
    EXPECT_GE(context.accepted.capacity(), context.accepted.size() + context.prepared.size());

    context.meshes[0u].positionBuffer = context.makeBuffer(Name("tests/shadow_trace_geometry/replacement"));
    context.hardwarePositions[0u] = context.meshes[0u].positionBuffer.get();
    ASSERT_TRUE(context.freeze(true, false));
    EXPECT_TRUE(context.accepted.empty());
    EXPECT_TRUE(context.prepared[0u].normalizationPending);
    EXPECT_NE(context.prepared[0u].buffer.get(), original.get());
    context.meshes.clear();
    context.hardwarePositions.clear();
    ASSERT_TRUE(context.freeze(true, false));
    EXPECT_TRUE(context.prepared.empty());
    EXPECT_EQ(original->getReferenceCount(), 1u);
}

TEST(ShadowTraceGeometry, RejectsMissingOrWrongMemberSelectionsAndClearsPreviousOutput){
    GeometryContext context;
    context.meshes.reserve(1u);
    context.addMesh(0u);
    context.hardwarePositions.push_back(context.meshes[0u].positionBuffer.get());
    ASSERT_TRUE(context.freeze(true, false));
    ASSERT_EQ(context.prepared.size(), 1u);

    context.hardwarePositions.push_back(context.meshes[0u].attributeBuffer.get());
    EXPECT_FALSE(context.freeze(true, false));
    EXPECT_TRUE(context.prepared.empty());
    context.hardwarePositions.back() = nullptr;
    EXPECT_FALSE(context.freeze(true, false));
    EXPECT_TRUE(context.prepared.empty());
    Core::BufferHandle unknown = context.makeBuffer(Name("tests/shadow_trace_geometry/unknown"));
    context.hardwarePositions.back() = unknown.get();
    EXPECT_FALSE(context.freeze(true, false));
    EXPECT_TRUE(context.prepared.empty());

    context.hardwarePositions.pop_back();
    context.softwarePositions.push_back(nullptr);
    ASSERT_TRUE(context.freeze(true, false));
    EXPECT_EQ(context.prepared.size(), 1u);
    EXPECT_FALSE(context.freeze(true, true));
    EXPECT_TRUE(context.prepared.empty());
    context.softwarePositions.clear();
    context.meshes[0u].runtimeMesh = true;
    context.meshes[0u].triangleIndexBuffer = nullptr;
    EXPECT_FALSE(context.freeze(true, false));
    EXPECT_TRUE(context.prepared.empty());
}

TEST(ShadowTraceGeometry, SharedGeometryUsesNoScratchAllocationsAndKeepsFirstIdentity){
    GeometryContext context;
    context.meshes.reserve(256u);
    context.addMesh(0u);
    const MeshSnapshot first = context.meshes.front();
    for(usize meshIndex = 1u; meshIndex < 256u; ++meshIndex){
        MeshSnapshot mesh = first;
        char indexText[32u] = {};
        mesh.meshName = DeriveName(first.meshName, FormatDecimal(meshIndex, indexText));
        mesh.runtimeMesh = true;
        context.meshes.push_back(Move(mesh));
    }
    context.hardwarePositions.push_back(first.positionBuffer.get());
    context.hardwareAttributes.push_back(first.attributeBuffer.get());
    context.softwareAttributes.push_back(first.attributeBuffer.get());
    context.accepted.push_back(first.positionBuffer);

    ASSERT_TRUE(context.freeze(true, true));
    ASSERT_EQ(context.prepared.size(), 4u);
    EXPECT_EQ(context.prepared[0u].identity, DeriveName(first.meshName, AStringView(":shadow_trace_hw_position")));
    EXPECT_EQ(context.prepared[0u].roles, Roles::HardwarePosition | Roles::SoftwarePosition);
    EXPECT_FALSE(context.prepared[0u].normalizationPending);
    EXPECT_EQ(context.prepared[1u].roles, Roles::HardwareAttribute | Roles::SoftwareAttribute);
    EXPECT_EQ(context.freezeArena.memoryStats().allocationCount, 0u);
    EXPECT_EQ(context.freezeArena.memoryStats().reservedBytes, 0u);
}

TEST(ShadowTraceGeometry, IndexedGeometryPreservesStablePruningMemberOwnershipAndRejection){
    GeometryContext context;
    context.meshes.reserve(16u);
    for(usize meshIndex = 0u; meshIndex < 16u; ++meshIndex)
        context.addMesh(meshIndex);
    Core::BufferHandle retiredFirst = context.makeBuffer(Name("tests/shadow_trace_geometry/retired_first"));
    Core::BufferHandle retiredMiddle = context.makeBuffer(Name("tests/shadow_trace_geometry/retired_middle"));
    context.meshes[0u].attributeBuffer = context.meshes[15u].positionBuffer;
    context.meshes[1u].attributeBuffer = context.meshes[0u].attributeBuffer;
    context.accepted.push_back(retiredFirst);
    context.accepted.push_back(context.meshes[10u].positionBuffer);
    context.accepted.push_back(retiredMiddle);
    context.accepted.push_back(context.meshes[0u].attributeBuffer);
    context.accepted.push_back(context.meshes[15u].swBvhNodeBuffer);
    context.accepted.push_back(context.meshes[10u].positionBuffer);
    context.hardwareAttributes.push_back(context.meshes[0u].attributeBuffer.get());

    ASSERT_TRUE(context.freeze(true, false));
    ASSERT_EQ(context.accepted.size(), 4u);
    EXPECT_EQ(context.accepted[0u].get(), context.meshes[10u].positionBuffer.get());
    EXPECT_EQ(context.accepted[1u].get(), context.meshes[0u].attributeBuffer.get());
    EXPECT_EQ(context.accepted[2u].get(), context.meshes[15u].swBvhNodeBuffer.get());
    EXPECT_EQ(context.accepted[3u].get(), context.meshes[10u].positionBuffer.get());
    EXPECT_EQ(retiredFirst->getReferenceCount(), 1u);
    EXPECT_EQ(retiredMiddle->getReferenceCount(), 1u);
    ASSERT_EQ(context.prepared.size(), 1u);
    EXPECT_EQ(context.prepared[0u].identity, DeriveName(context.meshes[0u].meshName, AStringView(":shadow_trace_hw_attribute")));
    EXPECT_FALSE(context.prepared[0u].normalizationPending);

    Swap(context.meshes[0u], context.meshes[1u]);
    ASSERT_TRUE(context.freeze(true, false));
    ASSERT_EQ(context.prepared.size(), 1u);
    EXPECT_EQ(context.prepared[0u].identity, DeriveName(context.meshes[0u].meshName, AStringView(":shadow_trace_hw_attribute")));
    context.hardwarePositions.push_back(context.meshes[7u].attributeBuffer.get());
    EXPECT_FALSE(context.freeze(true, false));
    EXPECT_TRUE(context.prepared.empty());
    EXPECT_EQ(context.accepted.size(), 4u);
    context.hardwarePositions.back() = retiredFirst.get();
    EXPECT_FALSE(context.freeze(true, false));
    EXPECT_TRUE(context.prepared.empty());
    context.hardwarePositions.clear();
    ASSERT_TRUE(context.freeze(true, false));
    ASSERT_EQ(context.prepared.size(), 1u);
    EXPECT_EQ(context.prepared[0u].roles, Roles::HardwareAttribute);
}

static void BenchmarkFreeze(const usize meshCount, const usize warmCount, const bool retireAll){
    GeometryContext context;
    context.meshes.reserve(meshCount);
    context.accepted.reserve(meshCount * 4u);
    context.hardwarePositions.reserve(meshCount);
    context.hardwareIndices.reserve(meshCount);
    context.hardwareAttributes.reserve(meshCount);
    context.softwareNodes.reserve(meshCount);
    context.softwarePositions.reserve(meshCount);
    context.softwareIndices.reserve(meshCount);
    context.softwareAttributes.reserve(meshCount);
    for(usize meshIndex = 0u; meshIndex < meshCount; ++meshIndex){
        context.addMesh(meshIndex);
        const MeshSnapshot& mesh = context.meshes.back();
        context.accepted.push_back(mesh.positionBuffer);
        context.accepted.push_back(mesh.triangleIndexBuffer);
        context.accepted.push_back(mesh.attributeBuffer);
        context.accepted.push_back(mesh.swBvhNodeBuffer);
        context.hardwarePositions.push_back(mesh.positionBuffer.get());
        context.hardwareIndices.push_back(mesh.triangleIndexBuffer.get());
        context.hardwareAttributes.push_back(mesh.attributeBuffer.get());
        context.softwareNodes.push_back(mesh.swBvhNodeBuffer.get());
        context.softwarePositions.push_back(mesh.positionBuffer.get());
        context.softwareIndices.push_back(mesh.triangleIndexBuffer.get());
        context.softwareAttributes.push_back(mesh.attributeBuffer.get());
    }
    if(retireAll)
        context.meshes.clear();

    const Timer coldBegin = TimerNow();
    const bool coldResult = context.freeze(!retireAll, !retireAll);
    const u64 coldNanoseconds = DurationInNS<u64>(TimerNow(), coldBegin);
    ASSERT_TRUE(coldResult);
    EXPECT_EQ(context.prepared.size(), retireAll ? 0u : meshCount * 4u);
    EXPECT_EQ(context.accepted.size(), retireAll ? 0u : meshCount * 4u);

    bool warmResult = true;
    const Timer warmBegin = TimerNow();
    for(usize iteration = 0u; iteration < warmCount; ++iteration){
        if(!context.freeze(!retireAll, !retireAll)){
            warmResult = false;
            break;
        }
    }
    const u64 warmNanoseconds = DurationInNS<u64>(TimerNow(), warmBegin);
    ASSERT_TRUE(warmResult);
    EXPECT_EQ(context.prepared.size(), retireAll ? 0u : meshCount * 4u);
    for(const Impl::PreparedShadowTraceGeometryBuffer& prepared : context.prepared){
        EXPECT_FALSE(prepared.normalizationPending);
        EXPECT_EQ(prepared.initialState, Core::ResourceStates::ShaderResource);
    }
    const ArenaMemoryStats scratchStats = context.freezeArena.memoryStats();
    char coldText[32u] = {};
    char warmText[32u] = {};
    char countText[32u] = {};
    char meshText[32u] = {};
    char reservedText[32u] = {};
    char peakText[32u] = {};
    testing::Test::RecordProperty("shadow_geometry_cold_ns", FormatDecimal(coldNanoseconds, coldText).data());
    testing::Test::RecordProperty("shadow_geometry_warm_ns", FormatDecimal(warmNanoseconds, warmText).data());
    testing::Test::RecordProperty("shadow_geometry_warm_count", FormatDecimal(warmCount, countText).data());
    testing::Test::RecordProperty("shadow_geometry_mesh_count", FormatDecimal(meshCount, meshText).data());
    testing::Test::RecordProperty("shadow_geometry_scratch_reserved_bytes", FormatDecimal(scratchStats.reservedBytes, reservedText).data());
    testing::Test::RecordProperty("shadow_geometry_scratch_peak_bytes", FormatDecimal(scratchStats.peakUsedBytes, peakText).data());
}

TEST(ShadowTraceGeometry, DISABLED_BenchmarkLargeHybridFreeze){
    BenchmarkFreeze(1024u, 4u, false);
}

TEST(ShadowTraceGeometry, DISABLED_BenchmarkSmallHybridFreeze){
    BenchmarkFreeze(1u, 256u, false);
}

TEST(ShadowTraceGeometry, DISABLED_BenchmarkRetiredGeometryPruning){
    BenchmarkFreeze(1024u, 0u, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

