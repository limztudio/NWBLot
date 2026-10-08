// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/mesh/compute_emulation_layout.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_compute_emulation_layout_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::ECSRenderDetail;

TEST(ComputeEmulationLayoutTests, TinyMeshReservesSentinelAndPadsTheUnifiedStructuredView){
    ComputeEmulationLayout layout;
    const auto resolvedLayout = ResolveComputeEmulationLayout(3u * sizeof(MeshletLocalVertexRef), 3u);
    ASSERT_TRUE(resolvedLayout);
    layout = *resolvedLayout;
    EXPECT_EQ(layout.indexByteOffset, 4u * NWB_MESH_EMULATION_VERTEX_BYTE_SIZE);
    EXPECT_EQ(layout.bufferByteSize, 5u * NWB_MESH_EMULATION_VERTEX_BYTE_SIZE);
    EXPECT_GE(layout.bufferByteSize, static_cast<u64>(layout.indexByteOffset) + 3u * sizeof(u32));
}


TEST(ComputeEmulationLayoutTests, RejectsInvalidSourceSizes){
    const u64 sizes[] = { 0u, 1u, sizeof(MeshletLocalVertexRef) + 1u, Limit<u64>::s_Max - 3u };
    for(const u64 size : sizes){
        EXPECT_FALSE(ResolveComputeEmulationLayout(size, 3u));
    }
    EXPECT_FALSE(ResolveComputeEmulationLayout(sizeof(MeshletLocalVertexRef), 0u));
}

TEST(ComputeEmulationLayoutTests, LastIndexWordMayEndAtTheUintAddressBoundary){
    constexpr u64 s_AddressableBytes = static_cast<u64>(Limit<u32>::s_Max) + 1u;
    constexpr u64 s_VertexCapacity = s_AddressableBytes / NWB_MESH_EMULATION_VERTEX_BYTE_SIZE - 1u;
    constexpr u64 s_RefBytes = (s_VertexCapacity - NWB_MESH_EMULATION_FIRST_VERTEX_INDEX) * sizeof(MeshletLocalVertexRef);
    ComputeEmulationLayout layout;
    const auto resolvedLayout = ResolveComputeEmulationLayout(s_RefBytes, 16u);
    ASSERT_TRUE(resolvedLayout);
    layout = *resolvedLayout;
    EXPECT_EQ(layout.indexByteOffset, s_AddressableBytes - NWB_MESH_EMULATION_VERTEX_BYTE_SIZE);
    EXPECT_EQ(layout.bufferByteSize, s_AddressableBytes);
    EXPECT_FALSE(ResolveComputeEmulationLayout(s_RefBytes, 17u));
    EXPECT_FALSE(ResolveComputeEmulationLayout(s_RefBytes + sizeof(MeshletLocalVertexRef), 1u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

