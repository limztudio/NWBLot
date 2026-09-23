// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets/graphics/mesh/object_geometry_constants.h>
#include <impl/assets_mesh/payload_types.h>
#include <impl/ecs_render/mesh/mesh_system.h>

#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_object_geometry_cache_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::ECSRenderDetail;

using TestArena = Tests::TestArena<struct ObjectGeometryCacheTestsTag>;
using SourceBufferMember = Core::BufferHandle RuntimeMeshBuffers::*;

constexpr SourceBufferMember s_SourceBufferMembers[] = {
    &RuntimeMeshBuffers::positionBuffer,
    &RuntimeMeshBuffers::normalBuffer,
    &RuntimeMeshBuffers::tangentBuffer,
    &RuntimeMeshBuffers::uv0Buffer,
    &RuntimeMeshBuffers::colorBuffer,
    &RuntimeMeshBuffers::meshletDescBuffer,
    &RuntimeMeshBuffers::meshletBoundsBuffer,
    &RuntimeMeshBuffers::meshletPositionRefDeltaBuffer,
    &RuntimeMeshBuffers::meshletAttributeRefDeltaBuffer,
    &RuntimeMeshBuffers::meshletLocalVertexRefBuffer,
    &RuntimeMeshBuffers::meshletPrimitiveIndexBuffer,
};

struct Context{
    TestArena testArena;
    Core::GraphicsAllocator graphicsAllocator{ testArena.arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    MeshResources mesh;
    RuntimeMeshBuffers source;
    ObjectGeometryCacheSnapshot snapshot;

    Context(){
        for(const SourceBufferMember member : s_SourceBufferMembers)
            mesh.*member = makeBuffer();
        mesh.runtimeMesh = true;
        mesh.runtimeGeometryContentRevision = 7u;
        mesh.objectGeometryCache.buffer = makeBuffer();
        mesh.objectGeometryCache.decoderPipeline = makeDecoder();
        mesh.objectGeometryCache.indexByteOffset = 4u * NWB_MESH_OBJECT_VERTEX_BYTE_SIZE;
        mesh.objectGeometryCache.indexCount = 3u;
        mesh.objectGeometryCache.heapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 8u);
        source = static_cast<const RuntimeMeshBuffers&>(mesh);
        snapshot = RendererMeshSystem::objectGeometryCacheSnapshot(mesh);
    }

    [[nodiscard]] Core::BufferHandle makeBuffer(){
        Core::Buffer* const buffer = Tests::NewMetadataOnlyBuffer(
            testArena.arena, context, allocator, Core::BufferDesc{}.setByteSize(256u).setCanHaveRawViews(true)
        );
        return Core::BufferHandle(buffer, Core::BufferHandle::deleter_type(&testArena.arena), AdoptRef);
    }

    [[nodiscard]] Core::ComputePipelineHandle makeDecoder(){
        Core::ComputePipeline* const pipeline = NewArenaObject<Core::ComputePipeline>(testArena.arena, context);
        return Core::ComputePipelineHandle(pipeline, Core::ComputePipelineHandle::deleter_type(&testArena.arena), AdoptRef);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ObjectGeometryCacheTests, LayoutKeepsSentinelVerticesAndPersistentIndicesInDisjointRegions){
    ObjectGeometryCacheLayout layout;
    ASSERT_TRUE(ResolveObjectGeometryCacheLayout(3u * sizeof(MeshletLocalVertexRef), 3u, layout));
    EXPECT_EQ(layout.indexByteOffset, 4u * NWB_MESH_OBJECT_VERTEX_BYTE_SIZE);
    EXPECT_EQ(layout.indexCount, 3u);
    EXPECT_EQ(layout.bufferByteSize, 5u * NWB_MESH_OBJECT_VERTEX_BYTE_SIZE);
    ASSERT_TRUE(ResolveObjectGeometryCacheLayout(96u * sizeof(MeshletLocalVertexRef), 192u, layout));
    EXPECT_EQ(layout.indexByteOffset, 97u * NWB_MESH_OBJECT_VERTEX_BYTE_SIZE);
    EXPECT_EQ(layout.indexCount, 192u);
    EXPECT_EQ(layout.bufferByteSize, layout.indexByteOffset + 192u * sizeof(u32));
}

TEST(ObjectGeometryCacheTests, LayoutRejectsEmptyMisalignedAndOverflowingInputs){
    const u64 sizes[] = { 0u, 1u, sizeof(MeshletLocalVertexRef) + 1u, Limit<u64>::s_Max - 3u };
    for(const u64 size : sizes){
        ObjectGeometryCacheLayout layout{ 123u, 12u, 3u };
        EXPECT_FALSE(ResolveObjectGeometryCacheLayout(size, 3u, layout));
        EXPECT_EQ(layout.bufferByteSize, 0u);
        EXPECT_EQ(layout.indexByteOffset, 0u);
        EXPECT_EQ(layout.indexCount, 0u);
    }
    ObjectGeometryCacheLayout layout;
    EXPECT_FALSE(ResolveObjectGeometryCacheLayout(sizeof(MeshletLocalVertexRef), 0u, layout));
    EXPECT_FALSE(ResolveObjectGeometryCacheLayout(sizeof(MeshletLocalVertexRef), Limit<u32>::s_Max, layout));
}

TEST(ObjectGeometryCacheTests, LayoutChecksFinalStructuredPaddingWithinUintByteAddressing){
    constexpr u64 s_AddressableBytes = static_cast<u64>(Limit<u32>::s_Max) + 1u;
    constexpr u64 s_MaximumRecords = s_AddressableBytes / NWB_MESH_OBJECT_VERTEX_BYTE_SIZE;
    // Reserve a full final structured record for the index region, including its view-alignment padding.
    constexpr u64 s_LastValidRefBytes = (s_MaximumRecords - 2u) * sizeof(MeshletLocalVertexRef);
    ObjectGeometryCacheLayout layout;
    ASSERT_TRUE(ResolveObjectGeometryCacheLayout(s_LastValidRefBytes, 3u, layout));
    EXPECT_LE(layout.bufferByteSize, s_AddressableBytes);
    EXPECT_EQ(layout.bufferByteSize % NWB_MESH_OBJECT_VERTEX_BYTE_SIZE, 0u);
    EXPECT_EQ(layout.indexByteOffset + 3u * sizeof(u32), layout.bufferByteSize - 36u);
    // The next raw index span still fits uint addressing; the required structured padding does not.
    EXPECT_LE(s_MaximumRecords * NWB_MESH_OBJECT_VERTEX_BYTE_SIZE + 3u * sizeof(u32), s_AddressableBytes);
    EXPECT_FALSE(ResolveObjectGeometryCacheLayout(s_LastValidRefBytes + sizeof(MeshletLocalVertexRef), 3u, layout));
    EXPECT_EQ(layout.bufferByteSize, 0u);
    EXPECT_EQ(layout.indexByteOffset, 0u);
    EXPECT_EQ(layout.indexCount, 0u);
}

TEST(ObjectGeometryCacheTests, RuntimeReuseRequiresAcceptedNonzeroCurrentContent){
    MeshResources mesh;
    mesh.runtimeMesh = true;
    mesh.runtimeGeometryContentRevision = 7u;
    EXPECT_TRUE(RendererMeshSystem::objectGeometryCacheSnapshot(mesh).requiresDecode);
    mesh.objectGeometryCache.initialized = true;
    mesh.objectGeometryCache.acceptedContent = true;
    mesh.objectGeometryCache.acceptedContentRevision = 7u;
    EXPECT_FALSE(RendererMeshSystem::objectGeometryCacheSnapshot(mesh).requiresDecode);
    mesh.runtimeGeometryContentRevision = 8u;
    EXPECT_TRUE(RendererMeshSystem::objectGeometryCacheSnapshot(mesh).requiresDecode);
    mesh.runtimeGeometryContentRevision = 0u;
    mesh.objectGeometryCache.acceptedContentRevision = 0u;
    const ObjectGeometryCacheSnapshot pending = RendererMeshSystem::objectGeometryCacheSnapshot(mesh);
    EXPECT_TRUE(pending.requiresDecode);
    EXPECT_TRUE(pending.initialized);
    mesh.runtimeGeometryContentRevision = 7u;
    mesh.objectGeometryCache.acceptedContentRevision = 7u;
    mesh.objectGeometryCache.acceptedContent = false;
    EXPECT_TRUE(RendererMeshSystem::objectGeometryCacheSnapshot(mesh).requiresDecode);
}

TEST(ObjectGeometryCacheTests, ImmutableStaticContentReusesZeroRevisionOnlyAfterAcceptance){
    MeshResources mesh;
    EXPECT_TRUE(RendererMeshSystem::objectGeometryCacheSnapshot(mesh).requiresDecode);
    mesh.objectGeometryCache.acceptedContent = true;
    mesh.objectGeometryCache.initialized = true;
    const ObjectGeometryCacheSnapshot accepted = RendererMeshSystem::objectGeometryCacheSnapshot(mesh);
    EXPECT_FALSE(accepted.requiresDecode);
    EXPECT_TRUE(accepted.initialized);
    EXPECT_EQ(accepted.sourceRevision, 0u);
    // A content decision never substitutes for the required retained resource and decoder bindings.
    EXPECT_FALSE(accepted.valid());
}


TEST(ObjectGeometryCacheTests, AcceptedPublicationMakesCurrentRuntimeAndStaticResourcesReusable){
    for(const bool runtime : { false, true }){
        SCOPED_TRACE(runtime);
        Context fixture;
        fixture.mesh.runtimeMesh = runtime;
        fixture.mesh.runtimeGeometryContentRevision = runtime ? 7u : 0u;
        fixture.snapshot = RendererMeshSystem::objectGeometryCacheSnapshot(fixture.mesh);
        ASSERT_TRUE(fixture.snapshot.valid());
        EXPECT_TRUE(fixture.snapshot.requiresDecode);
        ASSERT_TRUE(AcceptObjectGeometryCacheWrite(fixture.mesh, fixture.source, fixture.snapshot, runtime));
        EXPECT_TRUE(fixture.mesh.objectGeometryCache.initialized);
        EXPECT_TRUE(fixture.mesh.objectGeometryCache.acceptedContent);
        EXPECT_EQ(fixture.mesh.objectGeometryCache.acceptedContentRevision, fixture.snapshot.sourceRevision);
        const auto accepted = RendererMeshSystem::objectGeometryCacheSnapshot(fixture.mesh);
        EXPECT_TRUE(accepted.valid());
        EXPECT_TRUE(accepted.initialized);
        EXPECT_FALSE(accepted.requiresDecode);
    }
}

TEST(ObjectGeometryCacheTests, EachSourceReplacementInvalidatesAnAcceptedDecode){
    for(u32 index = 0u; index < LengthOf(s_SourceBufferMembers); ++index){
        SCOPED_TRACE(index);
        Context fixture;
        ASSERT_TRUE(AcceptObjectGeometryCacheWrite(fixture.mesh, fixture.source, fixture.snapshot, true));
        fixture.mesh.*s_SourceBufferMembers[index] = fixture.makeBuffer();
        EXPECT_FALSE(AcceptObjectGeometryCacheWrite(fixture.mesh, fixture.source, fixture.snapshot, true));
        EXPECT_TRUE(fixture.mesh.objectGeometryCache.initialized);
        EXPECT_FALSE(fixture.mesh.objectGeometryCache.acceptedContent);
        EXPECT_TRUE(RendererMeshSystem::objectGeometryCacheSnapshot(fixture.mesh).requiresDecode);
    }
}

TEST(ObjectGeometryCacheTests, AcceptedOlderWritesInitializeTheSameBufferWithoutValidatingNewContent){
    for(u32 mismatch = 0u; mismatch < 6u; ++mismatch){
        SCOPED_TRACE(mismatch);
        Context fixture;
        auto& cache = fixture.mesh.objectGeometryCache;
        switch(mismatch){
        case 0u: fixture.mesh.runtimeGeometryContentRevision = 8u; break;
        case 1u: cache.decoderPipeline = fixture.makeDecoder(); break;
        case 2u: cache.heapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 9u); break;
        case 3u: fixture.mesh.runtimeMesh = false; break;
        case 4u: cache.indexByteOffset += NWB_MESH_OBJECT_VERTEX_BYTE_SIZE; break;
        case 5u: cache.indexCount += 3u; break;
        }
        EXPECT_FALSE(cache.initialized);
        EXPECT_FALSE(AcceptObjectGeometryCacheWrite(fixture.mesh, fixture.source, fixture.snapshot, true));
        EXPECT_EQ(cache.buffer, fixture.snapshot.buffer);
        EXPECT_TRUE(cache.initialized);
        EXPECT_FALSE(cache.acceptedContent);
        EXPECT_TRUE(RendererMeshSystem::objectGeometryCacheSnapshot(fixture.mesh).requiresDecode);
    }
}

TEST(ObjectGeometryCacheTests, LateOlderContentInvalidatesPreviouslyAcceptedNewerContentInTheSameBuffer){
    Context fixture;
    fixture.mesh.runtimeGeometryContentRevision = 8u;
    const ObjectGeometryCacheSnapshot newer = RendererMeshSystem::objectGeometryCacheSnapshot(fixture.mesh);
    ASSERT_TRUE(AcceptObjectGeometryCacheWrite(fixture.mesh, fixture.source, newer, true));
    EXPECT_FALSE(RendererMeshSystem::objectGeometryCacheSnapshot(fixture.mesh).requiresDecode);
    EXPECT_FALSE(AcceptObjectGeometryCacheWrite(fixture.mesh, fixture.source, fixture.snapshot, true));
    EXPECT_TRUE(fixture.mesh.objectGeometryCache.initialized);
    EXPECT_FALSE(fixture.mesh.objectGeometryCache.acceptedContent);
    EXPECT_TRUE(RendererMeshSystem::objectGeometryCacheSnapshot(fixture.mesh).requiresDecode);
    EXPECT_EQ(fixture.mesh.objectGeometryCache.acceptedContentRevision, 8u);
    ASSERT_TRUE(AcceptObjectGeometryCacheWrite(fixture.mesh, fixture.source, newer, true));
    EXPECT_FALSE(RendererMeshSystem::objectGeometryCacheSnapshot(fixture.mesh).requiresDecode);
}

TEST(ObjectGeometryCacheTests, OldBufferAcceptanceCannotInitializeOrInvalidateAReplacementBuffer){
    for(const bool alreadyAccepted : { false, true }){
        SCOPED_TRACE(alreadyAccepted);
        Context fixture;
        auto& cache = fixture.mesh.objectGeometryCache;
        cache.buffer = fixture.makeBuffer();
        cache.initialized = alreadyAccepted;
        cache.acceptedContent = alreadyAccepted;
        cache.acceptedContentRevision = 19u;
        EXPECT_FALSE(AcceptObjectGeometryCacheWrite(fixture.mesh, fixture.source, fixture.snapshot, true));
        EXPECT_NE(cache.buffer, fixture.snapshot.buffer);
        EXPECT_EQ(cache.initialized, alreadyAccepted);
        EXPECT_EQ(cache.acceptedContent, alreadyAccepted);
        EXPECT_EQ(cache.acceptedContentRevision, 19u);
    }
}

TEST(ObjectGeometryCacheTests, InvalidSnapshotsNeverPublishBufferInitialization){
    for(u32 missing = 0u; missing < 6u; ++missing){
        SCOPED_TRACE(missing);
        Context fixture;
        switch(missing){
        case 0u: fixture.snapshot.buffer.reset(); break;
        case 1u: fixture.snapshot.decoderPipeline.reset(); break;
        case 2u: fixture.snapshot.heapHandle = Core::GpuDescriptorHandle::invalid(); break;
        case 3u: fixture.snapshot.heapHandle = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::UniformBuffer, 8u); break;
        case 4u: fixture.snapshot.indexByteOffset = 0u; break;
        case 5u: fixture.snapshot.indexCount = 0u; break;
        }
        EXPECT_FALSE(fixture.snapshot.valid());
        EXPECT_FALSE(AcceptObjectGeometryCacheWrite(fixture.mesh, fixture.source, fixture.snapshot, true));
        EXPECT_FALSE(fixture.mesh.objectGeometryCache.initialized);
        EXPECT_FALSE(fixture.mesh.objectGeometryCache.acceptedContent);
    }
}

TEST(ObjectGeometryCacheTests, UnknownRuntimeRevisionPublishesStateButRequiresAnotherDecode){
    Context fixture;
    fixture.mesh.runtimeGeometryContentRevision = 0u;
    fixture.snapshot = RendererMeshSystem::objectGeometryCacheSnapshot(fixture.mesh);
    ASSERT_TRUE(fixture.snapshot.valid());
    EXPECT_TRUE(AcceptObjectGeometryCacheWrite(fixture.mesh, fixture.source, fixture.snapshot, true));
    EXPECT_TRUE(fixture.mesh.objectGeometryCache.initialized);
    EXPECT_FALSE(fixture.mesh.objectGeometryCache.acceptedContent);
    EXPECT_EQ(fixture.mesh.objectGeometryCache.acceptedContentRevision, 0u);
    EXPECT_TRUE(RendererMeshSystem::objectGeometryCacheSnapshot(fixture.mesh).requiresDecode);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

