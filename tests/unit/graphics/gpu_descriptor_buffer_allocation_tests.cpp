// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/vulkan/descriptor_buffer_allocation_state.h>
#include <tests/common/test_context.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_descriptor_buffer_allocation_tests{
using namespace NWB;

TEST(GpuDescriptorBufferAllocation, CoalescingReusesRangeWithoutRevivingStaleIdentity){
    Tests::TestArena<> arena;
    Core::GraphicsBackend::DescriptorBufferAllocationState state(arena.arena);
    Array<u8, 64u> bytes{};
    using Kind = Core::GraphicsBackend::DescriptorBufferSegmentKind::Enum;
    constexpr Kind kind = Core::GraphicsBackend::DescriptorBufferSegmentKind::Resource;
    const auto first = state.allocate(kind, 16u, 8u, 41u, static_cast<u32>(bytes.size()), bytes.data());
    const auto middle = state.allocate(kind, 16u, 8u, 41u, static_cast<u32>(bytes.size()), bytes.data());
    const auto last = state.allocate(kind, 16u, 8u, 41u, static_cast<u32>(bytes.size()), bytes.data());
    ASSERT_TRUE(first && middle && last);
    state.free(*middle, 41u, static_cast<u32>(bytes.size()));
    state.free(*first, 41u, static_cast<u32>(bytes.size()));
    state.free(*last, 41u, static_cast<u32>(bytes.size()));
    const auto merged = state.allocate(kind, 48u, 8u, 41u, static_cast<u32>(bytes.size()), bytes.data());
    ASSERT_TRUE(merged);
    EXPECT_EQ(merged->offsetBytes, 0u);
    EXPECT_NE(merged->allocationSerial, first->allocationSerial);
    EXPECT_FALSE(state.isLive(*first, kind, 41u, static_cast<u32>(bytes.size())));
    EXPECT_FALSE(state.ownsAt(*first, first->offsetBytes));
    EXPECT_TRUE(state.isLive(*merged, kind, 41u, static_cast<u32>(bytes.size())));
    EXPECT_TRUE(state.ownsAt(*merged, merged->offsetBytes + 8u));
    state.resetOffset();
    state.clearAllocations();
    const auto afterReset = state.allocate(kind, 16u, 8u, 41u, static_cast<u32>(bytes.size()), bytes.data());
    ASSERT_TRUE(afterReset);
    EXPECT_NE(afterReset->allocationSerial, first->allocationSerial);
    EXPECT_FALSE(state.isLive(*first, kind, 41u, static_cast<u32>(bytes.size())));
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
