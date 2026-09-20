// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/soft_shadow_wavelet.h>

#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_soft_shadow_wavelet_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

struct WaveletInputsFixture{
    Tests::TestArena<> testArena;
    Core::GraphicsAllocator graphicsAllocator{ testArena.arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, 1u };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    Core::TextureHandle textures[7u];
    SoftShadowCombinedWaveletInputs inputs;

    WaveletInputsFixture(){
        const Name names[] = {
            Name("tests/wavelet/opaque_history"), Name("tests/wavelet/opaque_moments"),
            Name("tests/wavelet/transparent_history"), Name("tests/wavelet/transparent_moments"),
            Name("tests/wavelet/geometry"), Name("tests/wavelet/opaque_output"), Name("tests/wavelet/transparent_output")
        };
        for(usize index = 0u; index < LengthOf(textures); ++index){
            Core::TextureDesc desc;
            desc.setName(names[index]).setWidth(16u).setHeight(16u).setFormat(Core::Format::RGBA16_FLOAT);
            Core::Texture* const texture = Tests::NewMetadataOnlyTexture(testArena.arena, context, allocator, desc);
            textures[index] = Core::TextureHandle(texture, Core::TextureHandle::deleter_type(&testArena.arena), AdoptRef);
        }
        inputs = { textures[0u].get(), textures[1u].get(), textures[2u].get(), textures[3u].get(),
            textures[4u].get(), textures[5u].get(), textures[6u].get() };
    }
};

inline constexpr Core::Texture* SoftShadowCombinedWaveletInputs::* s_InputMembers[] = {
    &SoftShadowCombinedWaveletInputs::opaqueHistory,
    &SoftShadowCombinedWaveletInputs::opaqueMoments,
    &SoftShadowCombinedWaveletInputs::transparentHistory,
    &SoftShadowCombinedWaveletInputs::transparentMoments,
    &SoftShadowCombinedWaveletInputs::geometry,
    &SoftShadowCombinedWaveletInputs::opaqueOutput,
    &SoftShadowCombinedWaveletInputs::transparentOutput,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(SoftShadowWavelet, IndependentFreshTemporalInputsPermitExactlyOneWaveletPerChannel){
    WaveletInputsFixture fixture;
    EXPECT_TRUE(fixture.inputs.valid());
    EXPECT_TRUE(CanCombineSoftShadowWavelets(true, true, true, true, 1u, 1u));
    for(const u32 count : { 0u, 2u, 3u, 5u }){
        EXPECT_FALSE(CanCombineSoftShadowWavelets(true, true, true, true, count, 1u));
        EXPECT_FALSE(CanCombineSoftShadowWavelets(true, true, true, true, 1u, count));
    }
}

TEST(SoftShadowWavelet, EveryUnpreparedRouteRetainsSeparateWavelets){
    for(u32 flags = 0u; flags < 16u; ++flags){
        EXPECT_EQ(CanCombineSoftShadowWavelets(
            (flags & 1u) != 0u, (flags & 2u) != 0u, (flags & 4u) != 0u, (flags & 8u) != 0u,
            1u, 1u
        ), flags == 15u) << flags;
    }
}

TEST(SoftShadowWavelet, TargetCreationRejectsEveryMissingHistoryGeometryOrOutput){
    WaveletInputsFixture fixture;
    for(const auto member : s_InputMembers){
        SoftShadowCombinedWaveletInputs missing = fixture.inputs;
        missing.*member = nullptr;
        EXPECT_FALSE(missing.valid());
    }
}

TEST(SoftShadowWavelet, TargetCreationRejectsEveryAliasIncludingCrossChannelReadWriteHazards){
    WaveletInputsFixture fixture;
    for(usize index = 0u; index < LengthOf(s_InputMembers); ++index){
        for(usize previous = 0u; previous < index; ++previous){
            SoftShadowCombinedWaveletInputs alias = fixture.inputs;
            alias.*s_InputMembers[index] = alias.*s_InputMembers[previous];
            EXPECT_FALSE(alias.valid()) << index << ":" << previous;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

