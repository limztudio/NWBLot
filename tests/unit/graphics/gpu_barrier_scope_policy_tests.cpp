// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/vulkan/queue_barrier_scope_policy.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Tests{


TEST(QueueBarrierScopePolicy, TransferQueueRemovesUnsupportedShaderAccess){
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
    VkAccessFlags2 access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
    Core::GraphicsBackend::QueueBarrierScopePolicy::normalize(Core::GpuQueueCapability::Transfer, stages, access);
    EXPECT_EQ(stages, VK_PIPELINE_STAGE_2_COPY_BIT);
    EXPECT_EQ(access, VK_ACCESS_2_TRANSFER_READ_BIT);

    stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    access = VK_ACCESS_2_SHADER_READ_BIT;
    Core::GraphicsBackend::QueueBarrierScopePolicy::normalize(Core::GpuQueueCapability::Transfer, stages, access);
    EXPECT_EQ(stages, VK_PIPELINE_STAGE_2_NONE);
    EXPECT_EQ(access, 0u);
}


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
