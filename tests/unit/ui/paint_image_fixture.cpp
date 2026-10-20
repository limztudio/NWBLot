// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint_image_fixture.h"
#include "paint_assertions.h"
#include "image_fixture_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiPaintImageTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiWidgetTests;

constexpr AStringView s_PaintImageFontPath = "tests/ui/font";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ExpectSameImagePaint(const DrawSnapshot& actual, const DrawSnapshot& expected){
    EXPECT_FLOAT_EQ(actual.displayMetrics().logicalWidth, expected.displayMetrics().logicalWidth);
    EXPECT_FLOAT_EQ(actual.displayMetrics().logicalHeight, expected.displayMetrics().logicalHeight);
    EXPECT_FLOAT_EQ(actual.displayMetrics().pixelScaleX, expected.displayMetrics().pixelScaleX);
    EXPECT_FLOAT_EQ(actual.displayMetrics().pixelScaleY, expected.displayMetrics().pixelScaleY);
    EXPECT_EQ(actual.skinBinding().skin, expected.skinBinding().skin);
    EXPECT_EQ(actual.skinBinding().texture, expected.skinBinding().texture);
    EXPECT_EQ(actual.skinBinding().generation, expected.skinBinding().generation);
    EXPECT_EQ(actual.skinBinding().atlasWidth, expected.skinBinding().atlasWidth);
    EXPECT_EQ(actual.skinBinding().atlasHeight, expected.skinBinding().atlasHeight);
    EXPECT_FLOAT_EQ(actual.skinBinding().referenceDensity, expected.skinBinding().referenceDensity);
    ASSERT_EQ(actual.glyphPages().size(), expected.glyphPages().size());
    ASSERT_EQ(actual.sdfPages().size(), expected.sdfPages().size());
    ASSERT_EQ(actual.textureImages().size(), expected.textureImages().size());
    ASSERT_EQ(actual.vertices().size(), expected.vertices().size());
    ASSERT_EQ(actual.commands().size(), expected.commands().size());
    EXPECT_EQ(actual.indices(), expected.indices());
    for(usize index = 0u; index < actual.glyphPages().size(); ++index){
        ASSERT_TRUE(actual.glyphPages()[index]);
        ASSERT_TRUE(expected.glyphPages()[index]);
        EXPECT_EQ(actual.glyphPages()[index].get(), expected.glyphPages()[index].get());
        EXPECT_EQ(actual.glyphPages()[index]->binding(), expected.glyphPages()[index]->binding());
        EXPECT_EQ(actual.glyphPages()[index]->pixels(), expected.glyphPages()[index]->pixels());
    }
    for(usize index = 0u; index < actual.sdfPages().size(); ++index){
        ASSERT_TRUE(actual.sdfPages()[index]);
        ASSERT_TRUE(expected.sdfPages()[index]);
        EXPECT_EQ(actual.sdfPages()[index].get(), expected.sdfPages()[index].get());
        EXPECT_EQ(actual.sdfPages()[index]->binding(), expected.sdfPages()[index]->binding());
        EXPECT_EQ(actual.sdfPages()[index]->pixels(), expected.sdfPages()[index]->pixels());
    }
    for(usize index = 0u; index < actual.textureImages().size(); ++index){
        ASSERT_TRUE(actual.textureImages()[index]);
        ASSERT_TRUE(expected.textureImages()[index]);
        const ImageSource& a = *actual.textureImages()[index];
        const ImageSource& e = *expected.textureImages()[index];
        EXPECT_EQ(actual.textureImages()[index].get(), expected.textureImages()[index].get());
        EXPECT_EQ(a.identity(), e.identity());
        EXPECT_EQ(a.generation(), e.generation());
        EXPECT_EQ(a.texture().virtualPath(), e.texture().virtualPath());
        EXPECT_EQ(a.texture().width(), e.texture().width());
        EXPECT_EQ(a.texture().height(), e.texture().height());
        EXPECT_EQ(a.texture().dimension(), e.texture().dimension());
        EXPECT_EQ(a.texture().depth(), e.texture().depth());
        EXPECT_EQ(a.texture().payloadFormat(), e.texture().payloadFormat());
        EXPECT_EQ(a.texture().alphaMode(), e.texture().alphaMode());
        EXPECT_EQ(a.texture().alphaConstantUnorm8(), e.texture().alphaConstantUnorm8());
        EXPECT_EQ(a.texture().payloadBytes(), e.texture().payloadBytes());
        ASSERT_EQ(a.texture().mipLevels().size(), e.texture().mipLevels().size());
        for(usize mipIndex = 0u; mipIndex < a.texture().mipLevels().size(); ++mipIndex){
            const TextureMipLevel& am = a.texture().mipLevels()[mipIndex];
            const TextureMipLevel& em = e.texture().mipLevels()[mipIndex];
            EXPECT_EQ(am.width, em.width);
            EXPECT_EQ(am.height, em.height);
            EXPECT_EQ(am.blockCountX, em.blockCountX);
            EXPECT_EQ(am.blockCountY, em.blockCountY);
            EXPECT_EQ(am.offsetBytes, em.offsetBytes);
            EXPECT_EQ(am.sizeBytes, em.sizeBytes);
            EXPECT_EQ(am.sliceCount, em.sliceCount);
        }
    }
    UiPaintAssertions::ExpectSameVertices(actual, expected);
    for(usize index = 0u; index < actual.commands().size(); ++index){
        const DrawCommand& a = actual.commands()[index];
        const DrawCommand& e = expected.commands()[index];
        UiPaintAssertions::ExpectSameCommandFields(a, e);
        EXPECT_EQ(a.textureImageIndex, e.textureImageIndex);
        ExpectRect(a.clip, e.clip);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


PaintImageFixture::PaintImageFixture()
    : m_arena(Name("tests/ui/paint_image"))
    , m_skin(m_arena, Name(s_TestSkinPath))
    , m_builder(m_arena)
{
    UiSkin::RegionVector regions(m_arena);
    regions.push_back({ Name("sprite"), { 0u, 0u, 8u, 8u }, {}, {}, 0.0f, 0.0f, UiSkinDrawMode::Sprite });
    m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/skin_texture"), 16u, 16u, 1.0f, Move(regions));
    beginPaint(m_builder);
}

void PaintImageFixture::beginPaint(PaintBuilder& builder, const u64 generation){
    builder.begin({ 200.0f, 100.0f, 1.25f, 1.5f }, generation, 3u, Core::Assets::AssetRef<UiSkin>(s_TestSkinPath), m_skin);
    builder.reserve(16u);
}

Expected<SharedGlyphPage> PaintImageFixture::makeGlyph(const u64 atlasIdentity, const u64 generation){
    GlyphPage::Pixels pixels(m_arena);
    pixels.resize(64u, 0u);
    pixels[0u] = 64u;
    if(generation > 1u)
        pixels[32u] = 192u;
    const GlyphPageBinding binding{ .font = Core::Assets::AssetRef<Font>(s_PaintImageFontPath), .fontGeneration = 9u, .atlasIdentity = atlasIdentity, .generation = generation, .index = 0u, .width = 8u, .height = 8u, };
    return CreateGlyphPage(m_arena, binding, Move(pixels));
}

Expected<SharedSdfAtlasPage> PaintImageFixture::makeSdf(const u64 atlasIdentity){
    SdfAtlasPage::Pixels pixels(m_arena);
    pixels.resize(8u * 8u * 4u, 0u);
    pixels[0u] = 32u;
    pixels[1u] = 128u;
    pixels[2u] = 255u;
    pixels[3u] = 192u;
    const SdfAtlasPageBinding binding{
        .font = Core::Assets::AssetRef<Font>(s_PaintImageFontPath),
        .fontSha256 = {},
        .pixelsSha256 = {},
        .fontGeneration = 9u,
        .atlasIdentity = atlasIdentity,
        .generation = 1u,
        .index = 0u,
        .width = 8u,
        .height = 8u,
        .spreadPixels = 8u,
    };
    return CreateSdfAtlasPage(m_arena, binding, Move(pixels));
}

Expected<SharedImageSource> PaintImageFixture::makeImage(const StringView path, const u8 seed, const u32 width, const u32 height){
    if(path.empty() || width == 0u || height == 0u)
        return MakeUnexpected(Failure{});
    return UiFixtureSupport::MakeSeededImage(m_arena, path, seed, width, height);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

