// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/vulkan/blas_opacity_micromap_policy.h>
#include <core/alloc/scratch.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_blas_opacity_micromap_policy_tests{

using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Core::GraphicsBackend::VulkanDetail;

TEST(GpuBlasOpacityMicromapPolicyTests, AttachmentSkipsAbsentAndHandlesEmptyUsageCounts){
    Alloc::ScratchArena arena(NWB::Tests::s_TestArena);
    RayTracingGeometryDesc geometries[3];
    geometries[2].geometryData.triangles.numOmmUsageCounts = 2u;
    OpacityMicromapUsageOffsetVector offsets(arena);
    offsets.resize(3u);
    offsets[0] = Limit<usize>::s_Max;
    offsets[1] = offsets[2] = 0u;
    OpacityMicromapUsageVector usages(arena);
    usages.resize(2u);
    OpacityMicromapGeometryVector micromaps(arena);
    micromaps.resize(3u);
    BlasGeometryScratch scratch(arena);
    scratch.resizeForSizeQuery(3u);
    BlasOpacityMicromapPolicy::attachBlasOpacityMicromapGeometries(geometries, 3u, offsets, usages, micromaps, scratch);
    EXPECT_EQ(scratch.geometries[0].geometry.triangles.pNext, nullptr);
    EXPECT_EQ(scratch.geometries[1].geometry.triangles.pNext, &micromaps[1]);
    EXPECT_EQ(micromaps[1].pUsageCounts, nullptr);
    EXPECT_EQ(scratch.geometries[2].geometry.triangles.pNext, &micromaps[2]);
    EXPECT_EQ(micromaps[2].pUsageCounts, usages.data());
    const auto implicit = BlasOpacityMicromapPolicy::resolveOpacityMicromapIndexFormat(Format::UNKNOWN, {});
    ASSERT_TRUE(implicit);
    EXPECT_EQ(implicit->indexType, VK_INDEX_TYPE_NONE_KHR);
    EXPECT_EQ(implicit->indexStride, 0u);
}

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
