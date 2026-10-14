// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint_text_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_paint_sdf_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;


class UiPaintSdfTests : public NWB::UiPaintTextTests::PaintTextFixture{
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPaintSdfTests, ClipTrimsSdfGeometryAndUv){
    const SharedSdfAtlasPage page = makeSdfPage();
    ASSERT_TRUE(page);
    m_builder.pushClip({ 10.0f, 5.0f, 20.0f, 10.0f });
    ASSERT_TRUE(m_builder.drawSdfGlyph(page, 3u, { 0.0f, 0.0f, 40.0f, 20.0f }, { 0.125f, 0.25f, 0.5f, 0.5f }, { 0.8f, 0.4f, 0.2f, 0.5f }));
    ASSERT_TRUE(m_builder.popClip());
    ExpectClippedQuad(m_builder.freeze());
}

TEST_F(UiPaintSdfTests, BatchesOnlyAdjacentMatchingGroupChannelMaterialAndClip){
    const SharedSdfAtlasPage first = makeSdfPage();
    const SharedSdfAtlasPage second = makeSdfPage(11u, 1u, 1u);
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
    SharedSdfAtlasPage original = makeSdfPage();
    ASSERT_TRUE(original);
    const Sha256Digest oldPixelsHash = original->binding().pixelsSha256;
    ASSERT_TRUE(m_builder.drawSdfGlyph(original, 2u, { 1.0f, 1.0f, 4.0f, 4.0f }, { 0.0f, 0.0f, 0.25f, 0.25f }));
    const DrawSnapshot first = m_builder.freeze();
    original.reset();
    begin(8u);
    SharedSdfAtlasPage replacement = makeSdfPage(12u, 1u, 0u, 64u);
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
    const SharedSdfAtlasPage sdf = makeSdfPage();
    ASSERT_TRUE(sdf);
    ASSERT_TRUE(m_builder.prepareImages(coverage.data(), coverage.size(), &sdf, 1u));
    ASSERT_TRUE(m_builder.drawSdfGlyph(sdf, 3u, { 1.0f, 1.0f, 4.0f, 4.0f }, { 0.0f, 0.0f, 0.5f, 0.5f }));
    const SharedGlyphPage upgrade = makeCoverage(100u, 2u);
    const SharedSdfAtlasPage overflow = makeSdfPage(99u);
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
    const SharedSdfAtlasPage original = makeSdfPage();
    const SharedSdfAtlasPage conflicting = makeSdfPage(11u, 1u, 0u, 64u);
    ASSERT_TRUE(original && conflicting);
    ASSERT_TRUE(m_builder.prepareSdfPages(&original, 1u));
    EXPECT_FALSE(m_builder.prepareSdfPages(&conflicting, 1u));
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.sdfPages().size(), 1u);
    EXPECT_EQ(snapshot.sdfPages()[0].get(), original.get());
    EXPECT_TRUE(snapshot.vertices().empty());
}

TEST_F(UiPaintSdfTests, InvalidChannelAndUvFailBeforeImageOrGeometryAdmission){
    const SharedSdfAtlasPage page = makeSdfPage();
    ASSERT_TRUE(page);
    EXPECT_FALSE(m_builder.drawSdfGlyph(page, 4u, { 1.0f, 1.0f, 4.0f, 4.0f }, { 0.0f, 0.0f, 0.5f, 0.5f }));
    EXPECT_FALSE(m_builder.drawSdfGlyph(page, 0u, { 1.0f, 1.0f, 4.0f, 4.0f }, { 0.75f, 0.0f, 0.5f, 0.5f }));
    const DrawSnapshot snapshot = m_builder.freeze();
    EXPECT_TRUE(snapshot.sdfPages().empty());
    EXPECT_TRUE(snapshot.vertices().empty());
    EXPECT_TRUE(snapshot.commands().empty());
}

TEST_F(UiPaintSdfTests, DuplicateCandidatesConsumeOneImageSlot){
    const SharedSdfAtlasPage page = makeSdfPage();
    const SharedSdfAtlasPage reconstructed = makeSdfPage();
    ASSERT_TRUE(page && reconstructed);
    const Array<SharedSdfAtlasPage, 3u> candidates{ page, reconstructed, page };
    ASSERT_TRUE(m_builder.prepareSdfPages(candidates.data(), candidates.size()));
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.sdfPages().size(), 1u);
    EXPECT_EQ(snapshot.sdfPages()[0].get(), page.get());
}

TEST_F(UiPaintSdfTests, PageFactoryRejectsUnsupportedEncodingSpreadAndIncompleteRgba){
    SdfAtlasPageBinding binding = makeSdfPage()->binding();
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
    SdfAtlasPageBinding binding = makeSdfPage()->binding();
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
    EXPECT_EQ(snapshot.commands()[0].sdfChannel, 1u);
}

TEST_F(UiPaintSdfTests, CompactFactoryRejectsInvalidChannelCountAndRgbaSizedInput){
    SdfAtlasPageBinding binding = makeSdfPage()->binding();
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
        Core::Alloc::ScratchArena scratchArena(Name("tests/ui/paint_sdf/validation_scratch"));
        ASSERT_TRUE(atlas.validatePayload(scratchArena));
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

