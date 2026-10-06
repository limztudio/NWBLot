// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets/graphics/mesh/object_geometry_constants.h>
#include <impl/assets_mesh/payload_types.h>
#include <impl/ecs_render/mesh/mesh_system.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_object_geometry_cache_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::ECSRenderDetail;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ObjectGeometryCacheTests, LayoutKeepsSentinelVerticesAndPersistentIndicesInDisjointRegions){
    ObjectGeometryCacheLayout layout;
    ASSERT_TRUE(ResolveObjectGeometryCacheLayout(3u * sizeof(MeshletLocalVertexRef), 3u, layout));
    EXPECT_EQ(layout.indexByteOffset, 4u * NWB_MESH_OBJECT_VERTEX_BYTE_SIZE);
    EXPECT_EQ(layout.indexCount, 3u);
    EXPECT_EQ(layout.bufferByteSize, 5u * NWB_MESH_OBJECT_VERTEX_BYTE_SIZE);
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
    EXPECT_TRUE(RendererMeshSystem::ObjectGeometryCacheSnapshot(mesh).requiresDecode);
    mesh.objectGeometryCache.initialized = true;
    mesh.objectGeometryCache.acceptedContent = true;
    mesh.objectGeometryCache.acceptedContentRevision = 7u;
    EXPECT_FALSE(RendererMeshSystem::ObjectGeometryCacheSnapshot(mesh).requiresDecode);
    mesh.runtimeGeometryContentRevision = 8u;
    EXPECT_TRUE(RendererMeshSystem::ObjectGeometryCacheSnapshot(mesh).requiresDecode);
    mesh.runtimeGeometryContentRevision = 0u;
    mesh.objectGeometryCache.acceptedContentRevision = 0u;
    const ObjectGeometryCacheSnapshot pending = RendererMeshSystem::ObjectGeometryCacheSnapshot(mesh);
    EXPECT_TRUE(pending.requiresDecode);
    EXPECT_TRUE(pending.initialized);
    mesh.runtimeGeometryContentRevision = 7u;
    mesh.objectGeometryCache.acceptedContentRevision = 7u;
    mesh.objectGeometryCache.acceptedContent = false;
    EXPECT_TRUE(RendererMeshSystem::ObjectGeometryCacheSnapshot(mesh).requiresDecode);
}

TEST(ObjectGeometryCacheTests, ImmutableStaticContentReusesZeroRevisionOnlyAfterAcceptance){
    MeshResources mesh;
    EXPECT_TRUE(RendererMeshSystem::ObjectGeometryCacheSnapshot(mesh).requiresDecode);
    mesh.objectGeometryCache.acceptedContent = true;
    mesh.objectGeometryCache.initialized = true;
    const ObjectGeometryCacheSnapshot accepted = RendererMeshSystem::ObjectGeometryCacheSnapshot(mesh);
    EXPECT_FALSE(accepted.requiresDecode);
    EXPECT_TRUE(accepted.initialized);
    EXPECT_EQ(accepted.sourceRevision, 0u);
    // A content decision never substitutes for the required retained resource and decoder bindings.
    EXPECT_FALSE(accepted.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

