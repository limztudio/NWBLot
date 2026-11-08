// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/rhi/coopvec.h>
#include <core/graphics/vulkan/coopvec_feature_query.h>
#include <core/graphics/vulkan/coopvec_matrix_policy.h>
#include <tests/common/test_context.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cooperative_vector_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static u32 s_EmptyPropertyQueryCalls = 0u;

static VkResult VKAPI_CALL QueryNoCooperativeVectorProperties(VkPhysicalDevice, uint32_t* count, VkCooperativeVectorPropertiesNV* properties){
    ++s_EmptyPropertyQueryCalls;
    EXPECT_EQ(properties, nullptr);
    *count = 0u;
    return VK_SUCCESS;
}

TEST(CooperativeVectorFeatureQuery, EmptyDriverPropertyListDoesNotReadOrPublishFormats){
    NWB::Tests::TestArena<> arena;
    GraphicsAllocator allocator(arena.arena);
    CpuTaskScheduler scheduler(1u);
    GraphicsBackend::VulkanContext context(allocator, scheduler);
    context.extensions.nvCooperativeVector = true;
    context.coopVecFeatures.cooperativeVector = VK_TRUE;
    context.coopVecFeatures.cooperativeVectorTraining = VK_TRUE;
    context.coopVecProperties.cooperativeVectorTrainingFloat16Accumulation = VK_TRUE;
    context.instanceDispatch.vkGetPhysicalDeviceCooperativeVectorPropertiesNV = &QueryNoCooperativeVectorProperties;
    s_EmptyPropertyQueryCalls = 0u;
    const auto result = GraphicsBackend::CooperativeVectorFeatureQuery::query(context, scheduler);
    ASSERT_TRUE(result);
    EXPECT_EQ(s_EmptyPropertyQueryCalls, 1u);
    EXPECT_TRUE(result->matMulFormats.empty());
    EXPECT_FALSE(result->trainingFloat16);
    EXPECT_FALSE(result->trainingFloat32);
}

TEST(CooperativeVectorMatrixPolicy, RejectsTightMisalignedAndOverflowingStrides){
    using Policy = GraphicsBackend::CooperativeVectorMatrixPolicy;
    const auto layout = Policy::standardLayoutSize(4u, 2u, 3u, CooperativeVectorMatrixLayout::RowMajor, 0u);
    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->stride, 16u);
    EXPECT_EQ(layout->byteSize, 28u);
    EXPECT_FALSE(Policy::standardLayoutSize(4u, 2u, 3u, CooperativeVectorMatrixLayout::RowMajor, 12u));
    EXPECT_FALSE(Policy::standardLayoutSize(4u, 2u, 3u, CooperativeVectorMatrixLayout::RowMajor, 14u));
    EXPECT_FALSE(Policy::standardLayoutSize(Limit<usize>::s_Max, 2u, 3u, CooperativeVectorMatrixLayout::RowMajor, 0u));
    EXPECT_TRUE(Policy::isTypeConversionSupported(VK_COMPONENT_TYPE_FLOAT32_KHR, VK_COMPONENT_TYPE_FLOAT16_KHR));
    EXPECT_FALSE(Policy::isTypeConversionSupported(VK_COMPONENT_TYPE_FLOAT_E4M3_NV, VK_COMPONENT_TYPE_BFLOAT16_KHR));
}

TEST(CooperativeVectorMatrixStride, FailsClosedForInvalidOrDriverOptimalInputs){
    const auto inferencingStride = GetCooperativeVectorOptimalMatrixStride(CooperativeVectorDataType::Float32, CooperativeVectorMatrixLayout::InferencingOptimal, 2u, 2u);
    ASSERT_TRUE(inferencingStride);
    EXPECT_EQ(*inferencingStride, 0u);
    const auto trainingStride = GetCooperativeVectorOptimalMatrixStride(CooperativeVectorDataType::Float32, CooperativeVectorMatrixLayout::TrainingOptimal, 2u, 2u);
    ASSERT_TRUE(trainingStride);
    EXPECT_EQ(*trainingStride, 0u);
    EXPECT_FALSE(GetCooperativeVectorOptimalMatrixStride(CooperativeVectorDataType::Float32, CooperativeVectorMatrixLayout::RowMajor, 0u, 2u));
    EXPECT_FALSE(GetCooperativeVectorOptimalMatrixStride(CooperativeVectorDataType::Float32, CooperativeVectorMatrixLayout::ColumnMajor, 2u, 0u));
    EXPECT_FALSE(GetCooperativeVectorOptimalMatrixStride(static_cast<CooperativeVectorDataType::Enum>(Limit<u8>::s_Max), CooperativeVectorMatrixLayout::RowMajor, 2u, 2u));
    EXPECT_FALSE(GetCooperativeVectorOptimalMatrixStride(CooperativeVectorDataType::Float32, static_cast<CooperativeVectorMatrixLayout::Enum>(Limit<u8>::s_Max), 2u, 2u));
}

TEST(CooperativeVectorMatrixStride, DoesNotWrapLargestPublicDimension){
    const auto stride = GetCooperativeVectorOptimalMatrixStride(CooperativeVectorDataType::Float64, CooperativeVectorMatrixLayout::RowMajor, 1u, Limit<u32>::s_Max);
    if constexpr(sizeof(usize) > sizeof(u32)){
        ASSERT_TRUE(stride);
        EXPECT_EQ(*stride, (static_cast<usize>(Limit<u32>::s_Max) + 1u) * 8u);
    }
    else
        EXPECT_FALSE(stride);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

