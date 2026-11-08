// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/vulkan/vulkan_viewport_policy.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Tests{


TEST(VulkanViewportPolicy, FractionalBoundsAreRoundedOutAndNegativeScissorsAreRejected){
    using Policy = Core::GraphicsBackend::VulkanViewportPolicy;
    const Core::Viewport viewport(1.2f, 10.2f, 2.1f, 8.7f, 0.f, 1.f);
    const auto rect = Policy::BuildImplicitScissor(viewport);
    ASSERT_TRUE(rect);
    EXPECT_EQ(rect->offset.x, 1);
    EXPECT_EQ(rect->offset.y, 2);
    EXPECT_EQ(rect->extent.width, 10u);
    EXPECT_EQ(rect->extent.height, 7u);
    EXPECT_FALSE(Policy::BuildImplicitScissor(Core::Viewport(-0.1f, 10.f, 0.f, 8.f, 0.f, 1.f)));
}


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
