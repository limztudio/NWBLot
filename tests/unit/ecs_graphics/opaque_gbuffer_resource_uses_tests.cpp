// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/deferred/graph_resource_import_builder.h>
#include <impl/ecs_render/deferred/opaque_gbuffer_resource_uses.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_opaque_gbuffer_resource_uses_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(OpaqueGbufferResourceUses, CsgSparseValidityKeepsReadWriteRangesWithoutOpaqueDraws){
    Core::Alloc::ScratchArena scratch(Name("tests/opaque_gbuffer/csg_sparse"));
    DeferredGraphResourceImportResult resources;
    resources.csgCapBackNormal = { .generation = 1u, .index = 1u };
    resources.csgIntervalDepth = { .generation = 1u, .index = 2u };
    resources.csgIntervalId = { .generation = 1u, .index = 3u };
    resources.csgReceiverEventData = { .generation = 1u, .index = 4u };
    resources.csgReceiverEventCount = { .generation = 1u, .index = 5u };
    const Core::TextureSubresourceSet peel(0u, 1u, 0u, 4u);
    const Core::TextureSubresourceSet events(0u, 1u, 0u, 8u);
    const Core::TextureSubresourceSet counts(0u, 1u, 0u, 1u);
    const OpaqueGbufferResourceUseInputs inputs{
        .resources = resources,
        .materialInstanceRange = {},
        .materialTypedRange = {},
        .csgReceiverRange = {},
        .csgCutterRange = {},
        .csgPeelSubresources = peel,
        .csgReceiverEventDataSubresources = events,
        .csgReceiverEventCountSubresources = counts,
        .hasOpaqueDrawItems = false,
        .hasCsgFrameGpuWork = false,
        .hasOpaqueCsgFrameWork = true,
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{ scratch };
    OpaqueGbufferResourceUseBuilder::Append(inputs, uses);
    ASSERT_EQ(uses.size(), 11u);
    EXPECT_EQ(uses[1].resource, resources.csgCapBackNormal);
    EXPECT_EQ(uses[1].access, Core::GpuTaskResourceAccess::Write);
    EXPECT_EQ(uses[2].resource, resources.csgIntervalDepth);
    EXPECT_EQ(uses[2].access, Core::GpuTaskResourceAccess::Write);
    EXPECT_EQ(uses[3].resource, resources.csgIntervalId);
    EXPECT_EQ(uses[3].access, Core::GpuTaskResourceAccess::ReadWrite);
    EXPECT_EQ(uses[3].range.textureSubresources, peel);
    EXPECT_EQ(uses[4].resource, resources.csgReceiverEventData);
    EXPECT_EQ(uses[4].access, Core::GpuTaskResourceAccess::Write);
    EXPECT_EQ(uses[4].range.textureSubresources, events);
    EXPECT_EQ(uses[5].resource, resources.csgReceiverEventCount);
    EXPECT_EQ(uses[5].access, Core::GpuTaskResourceAccess::ReadWrite);
    EXPECT_EQ(uses[5].range.textureSubresources, counts);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

