// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint_text_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiPaintTextTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_PaintTextSkinPath = "tests/ui/skin";
constexpr AStringView s_PaintTextFontPath = "tests/ui/font";

void PaintTextFixture::ExpectClippedQuad(const DrawSnapshot& snapshot){
    ASSERT_EQ(snapshot.vertices().size(), 4u);
    const Vertex& first = snapshot.vertices()[0];
    const Vertex& opposite = snapshot.vertices()[2];
    EXPECT_FLOAT_EQ(first.position.x, 10.0f);
    EXPECT_FLOAT_EQ(first.position.y, 5.0f);
    EXPECT_FLOAT_EQ(opposite.position.x, 30.0f);
    EXPECT_FLOAT_EQ(opposite.position.y, 15.0f);
    EXPECT_FLOAT_EQ(first.texCoord.x, 0.25f);
    EXPECT_FLOAT_EQ(first.texCoord.y, 0.375f);
    EXPECT_FLOAT_EQ(opposite.texCoord.x, 0.5f);
    EXPECT_FLOAT_EQ(opposite.texCoord.y, 0.625f);
}


PaintTextFixture::PaintTextFixture()
    : m_arena(Name("tests/ui/paint_text"))
    , m_skin(m_arena, Name(s_PaintTextSkinPath))
    , m_builder(m_arena)
{
    UiSkin::RegionVector regions(m_arena);
    regions.push_back({ Name("sprite"), { 0u, 0u, 8u, 8u }, {}, {}, 0.0f, 0.0f, UiSkinDrawMode::Sprite });
    m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 16u, 16u, 1.0f, Move(regions));
    begin(7u);
}

void PaintTextFixture::begin(const u64 generation){
    m_builder.begin({ 100.0f, 80.0f, 1.5f, 2.0f }, generation, 3u, Core::Assets::AssetRef<UiSkin>(s_PaintTextSkinPath), m_skin);
    m_builder.reserve(16u);
}

Expected<SharedGlyphPage> PaintTextFixture::makeCoverage(const u64 atlasIdentity, const u64 generation){
    GlyphPage::Pixels pixels(m_arena);
    pixels.resize(4u, 128u);
    const GlyphPageBinding binding{ .font = Core::Assets::AssetRef<Font>(s_PaintTextFontPath), .fontGeneration = 9u, .atlasIdentity = atlasIdentity, .generation = generation, .width = 2u, .height = 2u, };
    return CreateGlyphPage(m_arena, binding, Move(pixels));
}

Expected<SharedGlyphPage> PaintTextFixture::makeGlyphPage(const u64 atlasIdentity, const u64 generation, const u32 index){
    GlyphPage::Pixels pixels(m_arena);
    pixels.resize(64u, 0u);
    pixels[0] = 64u;
    if(generation > 1u)
        pixels[32] = 192u;
    const GlyphPageBinding binding{ .font = Core::Assets::AssetRef<Font>(s_PaintTextFontPath), .fontGeneration = 9u, .atlasIdentity = atlasIdentity, .generation = generation, .index = index, .width = 8u, .height = 8u, };
    return CreateGlyphPage(m_arena, binding, Move(pixels));
}

Expected<SharedSdfAtlasPage> PaintTextFixture::makeDistance(){
    SdfAtlasPage::Pixels pixels(m_arena);
    pixels.resize(4u * 4u * 4u, 128u);
    const SdfAtlasPageBinding binding{ .font = Core::Assets::AssetRef<Font>(s_PaintTextFontPath), .fontSha256 = {}, .pixelsSha256 = {}, .fontGeneration = 9u, .atlasIdentity = 22u, .generation = 1u, .width = 4u, .height = 4u, .spreadPixels = 2u, };
    return CreateSdfAtlasPage(m_arena, binding, Move(pixels));
}

Expected<SharedSdfAtlasPage> PaintTextFixture::makeSdfPage(const u64 atlasIdentity, const u64 generation, const u32 index, const u8 interior){
    SdfAtlasPage::Pixels pixels(m_arena);
    pixels.resize(8u * 8u * 4u, 0u);
    pixels[0u] = 32u;
    pixels[1u] = 128u;
    pixels[2u] = interior;
    pixels[3u] = 192u;
    const SdfAtlasPageBinding binding{
        .font = Core::Assets::AssetRef<Font>(s_PaintTextFontPath),
        .fontSha256 = {},
        .pixelsSha256 = {},
        .fontGeneration = 9u,
        .atlasIdentity = atlasIdentity,
        .generation = generation,
        .index = index,
        .width = 8u,
        .height = 8u,
        .spreadPixels = 8u,
    };
    return CreateSdfAtlasPage(m_arena, binding, Move(pixels));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

