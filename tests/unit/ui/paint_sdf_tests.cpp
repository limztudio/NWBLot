// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/paint.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_paint_sdf_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;


class UiPaintSdfTests : public testing::Test{
public:
    UiPaintSdfTests()
        : m_arena(Name("tests/ui/paint_sdf"))
        , m_skin(m_arena, Name("tests/ui/skin"))
        , m_builder(m_arena)
    {
        UiSkin::RegionVector regions(m_arena);
        regions.push_back({ Name("sprite"), { 0u, 0u, 8u, 8u }, {}, {}, 0.0f, 0.0f, UiSkinDrawMode::Sprite });
        m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 16u, 16u, 1.0f, Move(regions));
        begin(7u);
    }


protected:
    void begin(const u64 generation){
        m_builder.begin({ 100.0f, 80.0f, 1.5f, 2.0f }, generation, 3u, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
        m_builder.reserve(16u);
    }

    [[nodiscard]] SharedSdfAtlasPage makePage(const u64 atlasIdentity = 11u, const u64 generation = 1u, const u32 index = 0u, const u8 interior = 255u){
        SdfAtlasPage::Pixels pixels(m_arena);
        pixels.resize(8u * 8u * 4u, 0u);
        pixels[0u] = 32u;
        pixels[1u] = 128u;
        pixels[2u] = interior;
        pixels[3u] = 192u;
        const SdfAtlasPageBinding binding{
            .font = Core::Assets::AssetRef<Font>("tests/ui/font"),
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

    [[nodiscard]] SharedGlyphPage makeCoverage(const u64 atlasIdentity, const u64 generation = 1u){
        GlyphPage::Pixels pixels(m_arena);
        pixels.resize(4u, 128u);
        const GlyphPageBinding binding{
            .font = Core::Assets::AssetRef<Font>("tests/ui/font"),
            .fontGeneration = 9u,
            .atlasIdentity = atlasIdentity,
            .generation = generation,
            .width = 2u,
            .height = 2u,
        };
        return CreateGlyphPage(m_arena, binding, Move(pixels));
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    UiSkin m_skin;
    PaintBuilder m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPaintSdfTests, ClipTrimsUvAndRetainsAlphaAsTheFourthDistanceChannel){
    const SharedSdfAtlasPage page = makePage();
    ASSERT_TRUE(page);
    m_builder.pushClip({ 10.0f, 5.0f, 20.0f, 10.0f });
    ASSERT_TRUE(m_builder.drawSdfGlyph(page, 3u, { 0.0f, 0.0f, 40.0f, 20.0f }, { 0.125f, 0.25f, 0.5f, 0.5f }, { 0.8f, 0.4f, 0.2f, 0.5f }));
    ASSERT_TRUE(m_builder.popClip());
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.vertices().size(), 4u);
    ASSERT_EQ(snapshot.indices().size(), 6u);
    ASSERT_EQ(snapshot.commands().size(), 1u);
    ASSERT_EQ(snapshot.sdfPages().size(), 1u);
    EXPECT_TRUE(snapshot.glyphPages().empty());
    EXPECT_EQ(snapshot.commands()[0].material, PaintMaterial::SdfGlyph);
    EXPECT_EQ(snapshot.commands()[0].glyphPageIndex, Limit<u32>::s_Max);
    EXPECT_EQ(snapshot.commands()[0].sdfPageIndex, 0u);
    EXPECT_EQ(snapshot.commands()[0].sdfChannel, 3u);
    EXPECT_EQ(snapshot.sdfPages()[0].get(), page.get());
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
    for(const Vertex& vertex : snapshot.vertices()){
        EXPECT_FLOAT_EQ(vertex.color.r, 0.4f);
        EXPECT_FLOAT_EQ(vertex.color.g, 0.2f);
        EXPECT_FLOAT_EQ(vertex.color.b, 0.1f);
        EXPECT_FLOAT_EQ(vertex.color.a, 0.5f);
    }
}

TEST_F(UiPaintSdfTests, BatchesOnlyAdjacentMatchingGroupChannelMaterialAndClip){
    const SharedSdfAtlasPage first = makePage();
    const SharedSdfAtlasPage second = makePage(11u, 1u, 1u);
    const SharedGlyphPage coverage = makeCoverage(22u);
    ASSERT_TRUE(first && second && coverage);
    const Rect rectangle{ 1.0f, 1.0f, 4.0f, 4.0f };
    const Rect uv{ 0.0f, 0.0f, 0.5f, 0.5f };
    ASSERT_TRUE(m_builder.drawSdfGlyph(first, 0u, rectangle, uv));
    ASSERT_TRUE(m_builder.drawSdfGlyph(first, 0u, rectangle, uv, { 0.5f, 0.25f, 1.0f, 0.5f }));
    ASSERT_TRUE(m_builder.drawSdfGlyph(first, 1u, rectangle, uv));
    ASSERT_TRUE(m_builder.drawSdfGlyph(second, 1u, rectangle, uv));
    ASSERT_TRUE(m_builder.drawGlyph(coverage, rectangle, uv));
    ASSERT_TRUE(m_builder.drawSdfGlyph(first, 0u, rectangle, uv));
    m_builder.fillRect(rectangle);
    ASSERT_TRUE(m_builder.drawRegion(Name("sprite"), rectangle));
    m_builder.pushClip({ 0.0f, 0.0f, 20.0f, 20.0f });
    ASSERT_TRUE(m_builder.drawSdfGlyph(first, 0u, rectangle, uv));
    ASSERT_TRUE(m_builder.popClip());
    ASSERT_TRUE(m_builder.drawSdfGlyph(first, 0u, rectangle, uv));
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.sdfPages().size(), 2u);
    ASSERT_EQ(snapshot.glyphPages().size(), 1u);
    ASSERT_EQ(snapshot.commands().size(), 9u);
    EXPECT_EQ(snapshot.commands()[0].indexCount, 12u);
    EXPECT_EQ(snapshot.commands()[1].sdfChannel, 1u);
    EXPECT_EQ(snapshot.commands()[2].sdfPageIndex, 1u);
    EXPECT_EQ(snapshot.commands()[3].material, PaintMaterial::Glyph);
    EXPECT_EQ(snapshot.commands()[4].material, PaintMaterial::SdfGlyph);
    EXPECT_EQ(snapshot.commands()[5].material, PaintMaterial::Solid);
    EXPECT_EQ(snapshot.commands()[6].material, PaintMaterial::Skin);
    EXPECT_FLOAT_EQ(snapshot.commands()[7].clip.width, 20.0f);
    EXPECT_FLOAT_EQ(snapshot.commands()[8].clip.width, 100.0f);
}

TEST_F(UiPaintSdfTests, FrozenSnapshotKeepsExactPixelsAndFontHashAcrossRepackAndRelease){
    SharedSdfAtlasPage original = makePage();
    ASSERT_TRUE(original);
    const Sha256Digest oldPixelsHash = original->binding().pixelsSha256;
    ASSERT_TRUE(m_builder.drawSdfGlyph(original, 2u, { 1.0f, 1.0f, 4.0f, 4.0f }, { 0.0f, 0.0f, 0.25f, 0.25f }));
    const DrawSnapshot first = m_builder.freeze();
    original.reset();
    begin(8u);
    SharedSdfAtlasPage replacement = makePage(12u, 1u, 0u, 64u);
    ASSERT_TRUE(replacement);
    ASSERT_TRUE(m_builder.drawSdfGlyph(replacement, 2u, { 6.0f, 1.0f, 4.0f, 4.0f }, { 0.5f, 0.5f, 0.25f, 0.25f }));
    const DrawSnapshot second = m_builder.freeze();
    replacement.reset();
    begin(9u);
    ASSERT_EQ(first.sdfPages().size(), 1u);
    ASSERT_EQ(second.sdfPages().size(), 1u);
    EXPECT_EQ(first.sdfPages()[0]->pixels()[2u], 255u);
    EXPECT_EQ(second.sdfPages()[0]->pixels()[2u], 64u);
    EXPECT_TRUE(first.sdfPages()[0]->binding().pixelsSha256 == oldPixelsHash);
    EXPECT_FALSE(first.sdfPages()[0]->binding().pixelsSha256 == second.sdfPages()[0]->binding().pixelsSha256);
    EXPECT_FLOAT_EQ(first.vertices()[0].texCoord.x, 0.0f);
    EXPECT_FLOAT_EQ(second.vertices()[0].texCoord.x, 0.5f);
}

TEST_F(UiPaintSdfTests, RejectedMixedAdmissionAtCapacityPreservesEarlierCoverageUpgradeAndGeometry){
    FixedVector<SharedGlyphPage, s_PaintMaxImages - 1u> coverage;
    for(usize index = 0u; index < coverage.max_size(); ++index){
        SharedGlyphPage page = makeCoverage(100u + index);
        ASSERT_TRUE(page);
        coverage.push_back(Move(page));
    }
    const SharedSdfAtlasPage sdf = makePage();
    ASSERT_TRUE(sdf);
    ASSERT_TRUE(m_builder.prepareImages(coverage.data(), coverage.size(), &sdf, 1u));
    ASSERT_TRUE(m_builder.drawSdfGlyph(sdf, 3u, { 1.0f, 1.0f, 4.0f, 4.0f }, { 0.0f, 0.0f, 0.5f, 0.5f }));
    const SharedGlyphPage upgrade = makeCoverage(100u, 2u);
    const SharedSdfAtlasPage overflow = makePage(99u);
    EXPECT_FALSE(m_builder.prepareImages(&upgrade, 1u, &overflow, 1u));
    EXPECT_FALSE(m_builder.drawSdfGlyph(overflow, 0u, { 10.0f, 10.0f, 4.0f, 4.0f }, { 0.0f, 0.0f, 0.5f, 0.5f }));
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.glyphPages().size(), s_PaintMaxImages - 1u);
    ASSERT_EQ(snapshot.sdfPages().size(), 1u);
    EXPECT_EQ(snapshot.glyphPages()[0].get(), coverage[0u].get());
    EXPECT_EQ(snapshot.glyphPages()[0]->binding().generation, 1u);
    EXPECT_EQ(snapshot.sdfPages()[0].get(), sdf.get());
    EXPECT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_EQ(snapshot.indices().size(), 6u);
    EXPECT_EQ(snapshot.commands().size(), 1u);
}

TEST_F(UiPaintSdfTests, EqualIdentityCannotAliasDifferentDistanceBytes){
    const SharedSdfAtlasPage original = makePage();
    const SharedSdfAtlasPage conflicting = makePage(11u, 1u, 0u, 64u);
    ASSERT_TRUE(original && conflicting);
    ASSERT_TRUE(m_builder.prepareSdfPages(&original, 1u));
    EXPECT_FALSE(m_builder.prepareSdfPages(&conflicting, 1u));
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.sdfPages().size(), 1u);
    EXPECT_EQ(snapshot.sdfPages()[0].get(), original.get());
    EXPECT_TRUE(snapshot.vertices().empty());
}

TEST_F(UiPaintSdfTests, InvalidChannelAndUvFailBeforeImageOrGeometryAdmission){
    const SharedSdfAtlasPage page = makePage();
    ASSERT_TRUE(page);
    EXPECT_FALSE(m_builder.drawSdfGlyph(page, 4u, { 1.0f, 1.0f, 4.0f, 4.0f }, { 0.0f, 0.0f, 0.5f, 0.5f }));
    EXPECT_FALSE(m_builder.drawSdfGlyph(page, 0u, { 1.0f, 1.0f, 4.0f, 4.0f }, { 0.75f, 0.0f, 0.5f, 0.5f }));
    const DrawSnapshot snapshot = m_builder.freeze();
    EXPECT_TRUE(snapshot.sdfPages().empty());
    EXPECT_TRUE(snapshot.vertices().empty());
    EXPECT_TRUE(snapshot.commands().empty());
}

TEST_F(UiPaintSdfTests, DuplicateCandidatesConsumeOneImageSlot){
    const SharedSdfAtlasPage page = makePage();
    const SharedSdfAtlasPage reconstructed = makePage();
    ASSERT_TRUE(page && reconstructed);
    const Array<SharedSdfAtlasPage, 3u> candidates{ page, reconstructed, page };
    ASSERT_TRUE(m_builder.prepareSdfPages(candidates.data(), candidates.size()));
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.sdfPages().size(), 1u);
    EXPECT_EQ(snapshot.sdfPages()[0].get(), page.get());
}

TEST_F(UiPaintSdfTests, PageFactoryRejectsUnsupportedEncodingSpreadAndIncompleteRgba){
    SdfAtlasPageBinding binding = makePage()->binding();
    SdfAtlasPage::Pixels pixels(m_arena);
    pixels.resize(256u, 0u);
    binding.distanceEncoding = 2u;
    EXPECT_FALSE(CreateSdfAtlasPage(m_arena, binding, Move(pixels)));
    EXPECT_EQ(pixels.size(), 256u);
    binding.distanceEncoding = s_SdfDistanceEncodingFreeTypeU8;
    binding.spreadPixels = s_FontAtlasMaxSpreadPixels + 1u;
    EXPECT_FALSE(CreateSdfAtlasPage(m_arena, binding, Move(pixels)));
    binding.spreadPixels = 8u;
    pixels.pop_back();
    EXPECT_FALSE(CreateSdfAtlasPage(m_arena, binding, Move(pixels)));
    EXPECT_EQ(pixels.size(), 255u);
}

TEST_F(UiPaintSdfTests, CompactPageRejectsMissingChannelsWithoutPublishingGeometry){
    SdfAtlasPageBinding binding = makePage()->binding();
    binding.width = 7u;
    binding.height = 5u;
    binding.channelCount = 2u;
    SdfAtlasPage::Pixels pixels(m_arena);
    pixels.resize(7u * 5u * 2u, 128u);
    const SharedSdfAtlasPage page = CreateSdfAtlasPage(m_arena, binding, Move(pixels));
    ASSERT_TRUE(page);
    EXPECT_FALSE(m_builder.drawSdfGlyph(page, 2u, { 1.f, 1.f, 4.f, 4.f }, { 0.f, 0.f, 0.5f, 0.5f }));
    EXPECT_TRUE(m_builder.freeze().commands().empty());
    begin(8u);
    ASSERT_TRUE(m_builder.drawSdfGlyph(page, 1u, { 1.f, 1.f, 4.f, 4.f }, { 0.f, 0.f, 0.5f, 0.5f }));
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.sdfPages().size(), 1u);
    EXPECT_EQ(snapshot.sdfPages()[0]->pixels().size(), 70u);
    EXPECT_EQ(snapshot.commands()[0].sdfChannel, 1u);
}

TEST_F(UiPaintSdfTests, CompactFactoryRejectsInvalidChannelCountAndRgbaSizedInput){
    SdfAtlasPageBinding binding = makePage()->binding();
    SdfAtlasPage::Pixels pixels(m_arena);
    pixels.resize(256u, 128u);
    binding.channelCount = 0u;
    EXPECT_FALSE(CreateSdfAtlasPage(m_arena, binding, Move(pixels)));
    binding.channelCount = 5u;
    EXPECT_FALSE(CreateSdfAtlasPage(m_arena, binding, Move(pixels)));
    binding.channelCount = 1u;
    EXPECT_FALSE(CreateSdfAtlasPage(m_arena, binding, Move(pixels)));
    EXPECT_EQ(pixels.size(), 256u);
    pixels.resize(64u);
    ASSERT_TRUE(CreateSdfAtlasPage(m_arena, binding, Move(pixels)));
}

TEST_F(UiPaintSdfTests, AssetFactoryCopiesExactGroupBytesBeforeSourceAssetIsReleased){
    SharedSdfAtlasPage page;
    Sha256Digest sourceHash;
    {
        FontAtlas atlas(m_arena, Name("tests/ui/font_atlas"));
        FontAtlasPayload payload(m_arena);
        payload.font = Core::Assets::AssetRef<Font>("tests/ui/font");
        payload.fontSha256.bytes[0u] = 99u;
        payload.unitsPerEm = 1000u;
        payload.sourceGlyphCount = 1u;
        payload.ascenderUnits = 800.f;
        payload.descenderUnits = -200.f;
        payload.glyphs.push_back({});
        FontAtlasGroup group(m_arena);
        group.width = 8u;
        group.height = 8u;
        group.pixels.resize(256u, 0u);
        group.pixels[3u] = 192u;
        group.sha256 = ComputeSha256(BinaryByteView(group.pixels.data(), group.pixels.size()));
        sourceHash = group.sha256;
        payload.groups.push_back(Move(group));
        atlas.setPayload(Move(payload));
        ASSERT_TRUE(atlas.validatePayload());
        page = CreateSdfAtlasPage(m_arena, atlas, 7u, 123u, 0u);
        ASSERT_TRUE(page);
    }
    ASSERT_TRUE(m_builder.drawSdfGlyph(page, 3u, { 1.0f, 1.0f, 4.0f, 4.0f }, { 0.0f, 0.0f, 0.5f, 0.5f }));
    const DrawSnapshot snapshot = m_builder.freeze();
    page.reset();
    ASSERT_EQ(snapshot.sdfPages().size(), 1u);
    EXPECT_EQ(snapshot.sdfPages()[0]->pixels()[3u], 192u);
    EXPECT_EQ(snapshot.sdfPages()[0]->binding().fontSha256.bytes[0u], 99u);
    EXPECT_TRUE(snapshot.sdfPages()[0]->binding().pixelsSha256 == sourceHash);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

