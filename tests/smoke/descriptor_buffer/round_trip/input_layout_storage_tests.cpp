// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"

#include <global/algorithm.h>
#include <global/arena_memory.h>
#include <global/text_utils.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Tests{


namespace __hidden_input_layout_storage_tests{

[[nodiscard]] static Array<VertexAttributeDesc, 3u> InputLayoutAttributes(){
    Array<VertexAttributeDesc, 3u> attributes{};
    attributes[0].setFormat(Format::R32_FLOAT).setBufferIndex(1u).setElementStride(8u);
    attributes[1].setFormat(Format::R32_FLOAT).setBufferIndex(1u).setOffset(4u).setElementStride(8u);
    attributes[2].setFormat(Format::R32_FLOAT).setBufferIndex(3u).setElementStride(4u).setIsInstanced(true);
    return attributes;
}

static void RecordInputLayoutProperty(const char* key, u64 value){
    char text[32u] = {};
    testing::Test::RecordProperty(key, FormatDecimal(value, text).data());
}

};


TEST_F(DescriptorBufferRoundTripTest, InputLayoutOwnsSharedAndSparseBindingDescriptions){
    auto attributes = __hidden_input_layout_storage_tests::InputLayoutAttributes();
    InputLayoutHandle layout = device().createInputLayout(attributes.data(), static_cast<u32>(attributes.size()), nullptr);
    ASSERT_TRUE(layout);
    attributes[0].setOffset(24u);
    attributes[2].setBufferIndex(7u);

    ASSERT_EQ(layout->getNumAttributes(), 3u);
    ASSERT_NE(layout->getAttributeDescription(0u), nullptr);
    EXPECT_EQ(layout->getAttributeDescription(0u)->offset, 0u);
    EXPECT_EQ(layout->getAttributeDescription(1u)->bufferIndex, 1u);
    EXPECT_EQ(layout->getAttributeDescription(1u)->offset, 4u);
    EXPECT_EQ(layout->getAttributeDescription(2u)->bufferIndex, 3u);
    EXPECT_TRUE(layout->getAttributeDescription(2u)->isInstanced);
    EXPECT_EQ(layout->getAttributeDescription(3u), nullptr);

    InputLayoutHandle second = device().createInputLayout(&attributes[1], 1u, nullptr);
    ASSERT_TRUE(second);
    layout.reset();
    ASSERT_EQ(second->getNumAttributes(), 1u);
    EXPECT_EQ(second->getAttributeDescription(0u)->offset, 4u);
}

TEST_F(DescriptorBufferRoundTripTest, InputLayoutSupportsEmptyDescription){
    InputLayoutHandle layout = device().createInputLayout(nullptr, 0u, nullptr);
    ASSERT_TRUE(layout);
    EXPECT_EQ(layout->getNumAttributes(), 0u);
    EXPECT_EQ(layout->getAttributeDescription(0u), nullptr);
}

// Opt in when comparing allocator implementations. This measures complete layout creation and destruction,
// including validation and scratch work, so a cheaper metadata allocation must improve the real operation.
TEST_F(DescriptorBufferRoundTripTest, DISABLED_InputLayoutCreationBenchmark){
    constexpr usize s_WarmupSamples = 8u;
    constexpr usize s_MeasuredSamples = 32u;
    constexpr usize s_Iterations = 2048u;
    const auto attributes = __hidden_input_layout_storage_tests::InputLayoutAttributes();
    Array<u64, s_MeasuredSamples> elapsed{};
    volatile u64 checksum = 0u;
    ArenaMemoryStats heapBefore{};
    for(usize sample = 0u; sample < s_WarmupSamples + s_MeasuredSamples; ++sample){
        if(sample == s_WarmupSamples)
            heapBefore = HeapBackingMemoryStats();
        const auto begin = TimerNow();
        for(usize iteration = 0u; iteration < s_Iterations; ++iteration){
            InputLayoutHandle layout = device().createInputLayout(attributes.data(), static_cast<u32>(attributes.size()), nullptr);
            ASSERT_TRUE(layout);
            checksum = checksum + layout->getNumAttributes() + layout->getAttributeDescription(2u)->bufferIndex;
        }
        if(sample >= s_WarmupSamples)
            elapsed[sample - s_WarmupSamples] = DurationInNS<u64>(TimerNow(), begin);
    }
    const auto heapAfter = HeapBackingMemoryStats();
    Sort(elapsed.begin(), elapsed.end());
    EXPECT_EQ(checksum, (s_WarmupSamples + s_MeasuredSamples) * s_Iterations * 6u);
    __hidden_input_layout_storage_tests::RecordInputLayoutProperty("median_ns", elapsed[15u] + (elapsed[16u] - elapsed[15u]) / 2u);
    __hidden_input_layout_storage_tests::RecordInputLayoutProperty("p95_ns", elapsed[30u]);
    __hidden_input_layout_storage_tests::RecordInputLayoutProperty("iterations_per_sample", s_Iterations);
    __hidden_input_layout_storage_tests::RecordInputLayoutProperty("heap_allocations_per_layout",
        (heapAfter.allocationCount - heapBefore.allocationCount) / (s_MeasuredSamples * s_Iterations)
    );
}


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

