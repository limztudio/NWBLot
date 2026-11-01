// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/mesh/instance_gpu_data.adhd.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets_mesh/asset.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_compute_emulation_layout_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

TEST(ComputeEmulationLayoutTests, TinyMeshReservesSentinelAndPadsTheUnifiedStructuredView){
    ComputeEmulationLayout layout{};
    ASSERT_TRUE(TryResolveComputeEmulationLayout(3u * sizeof(MeshletLocalVertexRef), 3u, layout));
    EXPECT_EQ(layout.indexByteOffset, 4u * NWB_MESH_EMULATION_VERTEX_BYTE_SIZE);
    EXPECT_EQ(layout.bufferByteSize, 5u * NWB_MESH_EMULATION_VERTEX_BYTE_SIZE);
    EXPECT_GE(layout.bufferByteSize, static_cast<u64>(layout.indexByteOffset) + 3u * sizeof(u32));
}

TEST(ComputeEmulationLayoutTests, RejectsInvalidSourceSizes){
    ComputeEmulationLayout layout{};
    const Array<u64, 4> sizes = { 0u, 1u, sizeof(MeshletLocalVertexRef) + 1u, Limit<u64>::s_Max - 3u };
    for(const u64 size : sizes)
        EXPECT_FALSE(TryResolveComputeEmulationLayout(size, 3u, layout));
    EXPECT_FALSE(TryResolveComputeEmulationLayout(sizeof(MeshletLocalVertexRef), 0u, layout));
}

TEST(ComputeEmulationLayoutTests, LastIndexWordMayEndAtTheUintAddressBoundary){
    constexpr u64 s_AddressableBytes = static_cast<u64>(Limit<u32>::s_Max) + 1u;
    constexpr u64 s_VertexCapacity = s_AddressableBytes / NWB_MESH_EMULATION_VERTEX_BYTE_SIZE - 1u;
    constexpr u64 s_RefBytes = (s_VertexCapacity - NWB_MESH_EMULATION_FIRST_VERTEX_INDEX) * sizeof(MeshletLocalVertexRef);
    ComputeEmulationLayout layout{};
    ASSERT_TRUE(TryResolveComputeEmulationLayout(s_RefBytes, 16u, layout));
    EXPECT_EQ(layout.indexByteOffset, s_AddressableBytes - NWB_MESH_EMULATION_VERTEX_BYTE_SIZE);
    EXPECT_EQ(layout.bufferByteSize, s_AddressableBytes);
    EXPECT_FALSE(TryResolveComputeEmulationLayout(s_RefBytes, 17u, layout));
    EXPECT_FALSE(TryResolveComputeEmulationLayout(s_RefBytes + sizeof(MeshletLocalVertexRef), 1u, layout));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

