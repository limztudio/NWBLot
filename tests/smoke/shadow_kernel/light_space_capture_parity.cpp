// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_kernel_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LightSpaceKernel{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void CheckCaptureParity(GraphicsBackend::Device& device, Buffer& referenceCounts, Buffer& referenceEvents,
    StagingTexture& referenceDepth, Buffer& counts, Buffer& events, StagingTexture& depth,
    const u32 pixelCount, const u32 viewCount, const bool corruptedEvents){
    const auto* expectedCounts = static_cast<const u32*>(device.mapBuffer(referenceCounts, CpuAccessMode::Read));
    const auto* actualCounts = static_cast<const u32*>(device.mapBuffer(counts, CpuAccessMode::Read));
    ASSERT_NE(expectedCounts, nullptr);
    ASSERT_NE(actualCounts, nullptr);
    EXPECT_EQ(NWB_MEMCMP(expectedCounts, actualCounts, (pixelCount + 1u) * sizeof(u32)), 0);
    const auto* expectedEvents = static_cast<const Event*>(device.mapBuffer(referenceEvents, CpuAccessMode::Read));
    const auto* actualEvents = static_cast<const Event*>(device.mapBuffer(events, CpuAccessMode::Read));
    ASSERT_NE(expectedEvents, nullptr);
    ASSERT_NE(actualEvents, nullptr);
    if(!corruptedEvents){
        for(u32 pixel = 0u; pixel < pixelCount; ++pixel){
            const u32 count = expectedCounts[pixel];
            // Overflow retains an unordered atomic subset; only completed, sorted texels have a canonical record sequence.
            if(count > NWB_LIGHT_SPACE_EVENTS_PER_TEXEL)
                continue;
            const u32 first = pixel * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL;
            EXPECT_EQ(NWB_MEMCMP(expectedEvents + first, actualEvents + first, count * sizeof(Event)), 0) << "pixel " << pixel;
        }
    }
    const u32 guard = pixelCount * NWB_LIGHT_SPACE_EVENTS_PER_TEXEL;
    EXPECT_EQ(NWB_MEMCMP(expectedEvents + guard, actualEvents + guard, sizeof(Event)), 0);
    device.unmapBuffer(events);
    device.unmapBuffer(referenceEvents);
    device.unmapBuffer(counts);
    device.unmapBuffer(referenceCounts);
    for(u32 layer = 0u; layer < viewCount; ++layer){
        const TextureSlice slice = TextureSlice{}.setArraySlice(layer);
        usize expectedPitch = 0u;
        usize actualPitch = 0u;
        const u8* expected = static_cast<const u8*>(device.mapStagingTexture(referenceDepth, slice, CpuAccessMode::Read, &expectedPitch));
        const u8* actual = static_cast<const u8*>(device.mapStagingTexture(depth, slice, CpuAccessMode::Read, &actualPitch));
        ASSERT_NE(expected, nullptr);
        ASSERT_NE(actual, nullptr);
        for(u32 y = 0u; y < s_MapSize; ++y)
            EXPECT_EQ(NWB_MEMCMP(expected + y * expectedPitch, actual + y * actualPitch, s_MapSize * sizeof(f32)), 0) << "layer " << layer << " row " << y;
        device.unmapStagingTexture(depth);
        device.unmapStagingTexture(referenceDepth);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

