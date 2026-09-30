// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint_image_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/image.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiPaintImageTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TextureImageLayoutFixture = PaintImageFixture;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(TextureImageLayoutFixture, NaturalExtentIsOneLogicalUnitPerTextureTexel){
    const SharedImageSource image = makeImage("tests/ui/layout_image", 37u, 20u, 12u);
    ASSERT_TRUE(image);
    ImageMetrics metrics;
    ASSERT_TRUE(ImageLayout::Measure({}, *image, metrics));
    EXPECT_FLOAT_EQ(metrics.contentSize.x, 20.0f);
    EXPECT_FLOAT_EQ(metrics.contentSize.y, 12.0f);
    EXPECT_EQ(image->texture().mipLevels().size(), 5u);
}

TEST_F(TextureImageLayoutFixture, FixedAndStretchPoliciesKeepTheSourceIntrinsicExtent){
    const SharedImageSource image = makeImage("tests/ui/layout_image", 37u, 20u, 12u);
    ASSERT_TRUE(image);
    ImageOptions options;
    options.width = { LayoutSizePolicy::Fixed, 96.0f };
    options.height = { LayoutSizePolicy::Stretch, 2.0f };
    ImageMetrics metrics;
    ASSERT_TRUE(ImageLayout::Measure(options, *image, metrics));
    EXPECT_FLOAT_EQ(metrics.contentSize.x, 20.0f);
    EXPECT_FLOAT_EQ(metrics.contentSize.y, 12.0f);
    options.width = { LayoutSizePolicy::Stretch, 1.0f };
    options.height = { LayoutSizePolicy::Fixed, 32.0f };
    ASSERT_TRUE(ImageLayout::Measure(options, *image, metrics));
    EXPECT_FLOAT_EQ(metrics.contentSize.x, 20.0f);
    EXPECT_FLOAT_EQ(metrics.contentSize.y, 12.0f);
}

TEST_F(TextureImageLayoutFixture, SameIdentityVersionsMeasureTheirOwnImmutableDimensions){
    const SharedImageSource first = makeImage("tests/ui/layout_version", 37u, 20u, 12u);
    const SharedImageSource second = makeImage("tests/ui/layout_version", 73u, 9u, 7u);
    ASSERT_TRUE(first && second);
    ASSERT_EQ(first->identity(), second->identity());
    ASSERT_NE(first->generation(), second->generation());
    ImageMetrics metrics;
    ASSERT_TRUE(ImageLayout::Measure({}, *first, metrics));
    EXPECT_FLOAT_EQ(metrics.contentSize.x, 20.0f);
    EXPECT_FLOAT_EQ(metrics.contentSize.y, 12.0f);
    ASSERT_TRUE(ImageLayout::Measure({}, *second, metrics));
    EXPECT_FLOAT_EQ(metrics.contentSize.x, 9.0f);
    EXPECT_FLOAT_EQ(metrics.contentSize.y, 7.0f);
    ASSERT_TRUE(ImageLayout::Measure({}, *first, metrics));
    EXPECT_FLOAT_EQ(metrics.contentSize.x, 20.0f);
    EXPECT_FLOAT_EQ(metrics.contentSize.y, 12.0f);
}

TEST_F(TextureImageLayoutFixture, EmptyFixedExtentAndTransparentHdrTintRemainValid){
    const SharedImageSource image = makeImage();
    ASSERT_TRUE(image);
    ImageOptions options;
    options.width = { LayoutSizePolicy::Fixed, 0.0f };
    options.height = { LayoutSizePolicy::Fixed, 0.0f };
    options.tint = { 2.0f, 0.0f, 0.5f, 0.0f };
    ImageMetrics metrics;
    ASSERT_TRUE(ImageLayout::Measure(options, *image, metrics));
    EXPECT_FLOAT_EQ(metrics.contentSize.x, 8.0f);
    EXPECT_FLOAT_EQ(metrics.contentSize.y, 4.0f);
    ImagePlacement placement;
    ASSERT_TRUE(ImageLayout::Place({ 10.0f, 20.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 100.0f, 100.0f }, placement));
    EXPECT_FLOAT_EQ(placement.bounds.width, 0.0f);
    EXPECT_FLOAT_EQ(placement.clip.height, 0.0f);
}

TEST_F(TextureImageLayoutFixture, InvalidSizePoliciesAndValuesPreserveBothOutputComponents){
    const SharedImageSource image = makeImage();
    ASSERT_TRUE(image);
    const ImageMetrics accepted{ { 31.0f, 47.0f } };
    for(u32 field = 0u; field < 8u; ++field){
        ImageOptions options;
        switch(field){
        case 0u: options.width.policy = static_cast<LayoutSizePolicy::Enum>(3u); break;
        case 1u: options.height.policy = static_cast<LayoutSizePolicy::Enum>(3u); break;
        case 2u: options.width.value = -1.0f; break;
        case 3u: options.height.value = -1.0f; break;
        case 4u: options.width = { LayoutSizePolicy::Stretch, 0.0f }; break;
        case 5u: options.height = { LayoutSizePolicy::Stretch, 0.0f }; break;
        case 6u: options.width.value = Limit<f32>::s_QuietNaN; break;
        default: options.height.value = Limit<f32>::s_Infinity; break;
        }
        ImageMetrics output = accepted;
        EXPECT_FALSE(ImageLayout::Measure(options, *image, output));
        EXPECT_EQ(BitCast<u32>(output.contentSize.x), BitCast<u32>(accepted.contentSize.x));
        EXPECT_EQ(BitCast<u32>(output.contentSize.y), BitCast<u32>(accepted.contentSize.y));
    }
}

TEST_F(TextureImageLayoutFixture, InvalidTintPreservesThePreviousMeasuredExtent){
    const SharedImageSource image = makeImage();
    ASSERT_TRUE(image);
    ImageMetrics accepted;
    ASSERT_TRUE(ImageLayout::Measure({}, *image, accepted));
    for(u32 field = 0u; field < 8u; ++field){
        ImageOptions options;
        switch(field){
        case 0u: options.tint.r = -1.0f; break;
        case 1u: options.tint.g = -1.0f; break;
        case 2u: options.tint.b = -1.0f; break;
        case 3u: options.tint.r = Limit<f32>::s_Infinity; break;
        case 4u: options.tint.g = Limit<f32>::s_QuietNaN; break;
        case 5u: options.tint.b = Limit<f32>::s_Infinity; break;
        case 6u: options.tint.a = -0.01f; break;
        default: options.tint.a = 1.01f; break;
        }
        ImageMetrics output = accepted;
        EXPECT_FALSE(ImageLayout::Measure(options, *image, output));
        EXPECT_EQ(BitCast<u32>(output.contentSize.x), BitCast<u32>(accepted.contentSize.x));
        EXPECT_EQ(BitCast<u32>(output.contentSize.y), BitCast<u32>(accepted.contentSize.y));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

