// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/paint.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_paint_glyph_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;


class UiPaintGlyphTests : public testing::Test{
public:
    UiPaintGlyphTests()
        : m_arena(Name("tests/ui/paint_glyph"))
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

    [[nodiscard]] SharedGlyphPage makePage(const u64 atlasIdentity = 11u, const u64 generation = 1u, const u32 index = 0u){
        GlyphPage::Pixels pixels(m_arena);
        pixels.resize(64u, 0u);
        pixels[0] = 64u;
        if(generation > 1u)
            pixels[32] = 192u;
        const GlyphPageBinding binding{
            .font = Core::Assets::AssetRef<Font>("tests/ui/font"),
            .fontGeneration = 9u,
            .atlasIdentity = atlasIdentity,
            .generation = generation,
            .index = index,
            .width = 8u,
            .height = 8u,
        };
        return CreateGlyphPage(m_arena, binding, Move(pixels));
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    UiSkin m_skin;
    PaintBuilder m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPaintGlyphTests, ClipTrimsGlyphGeometryAndUv){
    const SharedGlyphPage page = makePage();
    ASSERT_TRUE(page);
    m_builder.pushClip({ 10.0f, 5.0f, 20.0f, 10.0f });
    ASSERT_TRUE(m_builder.drawGlyph(page, { 0.0f, 0.0f, 40.0f, 20.0f }, { 0.125f, 0.25f, 0.5f, 0.5f }, { 0.8f, 0.4f, 0.2f, 0.5f }));
    ASSERT_TRUE(m_builder.popClip());
    const DrawSnapshot snapshot = m_builder.freeze();
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

TEST_F(UiPaintGlyphTests, BatchesOnlyAdjacentGlyphsWithMatchingPageMaterialAndClip){
    const SharedGlyphPage first = makePage();
    const SharedGlyphPage second = makePage(11u, 1u, 1u);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    const Rect rectangle{ 1.0f, 1.0f, 4.0f, 4.0f };
    const Rect uv{ 0.0f, 0.0f, 0.5f, 0.5f };
    ASSERT_TRUE(m_builder.drawGlyph(first, rectangle, uv));
    ASSERT_TRUE(m_builder.drawGlyph(first, rectangle, uv, { 0.5f, 0.25f, 1.0f, 0.5f }));
    ASSERT_TRUE(m_builder.drawGlyph(second, rectangle, uv));
    ASSERT_TRUE(m_builder.drawGlyph(first, rectangle, uv));
    m_builder.fillRect(rectangle);
    ASSERT_TRUE(m_builder.drawGlyph(first, rectangle, uv));
    ASSERT_TRUE(m_builder.drawRegion(Name("sprite"), rectangle));
    ASSERT_TRUE(m_builder.drawGlyph(first, rectangle, uv));
    m_builder.pushClip({ 0.0f, 0.0f, 20.0f, 20.0f });
    ASSERT_TRUE(m_builder.drawGlyph(first, rectangle, uv));
    ASSERT_TRUE(m_builder.popClip());
    ASSERT_TRUE(m_builder.drawGlyph(first, rectangle, uv));
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.glyphPages().size(), 2u);
    ASSERT_EQ(snapshot.commands().size(), 9u);
    ASSERT_EQ(snapshot.vertices().size(), 40u);
    ASSERT_EQ(snapshot.indices().size(), 60u);
    EXPECT_EQ(snapshot.commands()[0].indexCount, 12u);
    EXPECT_EQ(snapshot.commands()[1].glyphPageIndex, 1u);
    EXPECT_EQ(snapshot.commands()[2].glyphPageIndex, 0u);
    EXPECT_EQ(snapshot.commands()[3].material, PaintMaterial::Solid);
    EXPECT_EQ(snapshot.commands()[3].glyphPageIndex, Limit<u32>::s_Max);
    EXPECT_EQ(snapshot.commands()[5].material, PaintMaterial::Skin);
    EXPECT_EQ(snapshot.commands()[5].glyphPageIndex, Limit<u32>::s_Max);
    for(usize index = 1u; index < snapshot.commands().size(); ++index){
        EXPECT_EQ(snapshot.commands()[index].firstIndex, static_cast<u32>((index + 1u) * 6u));
        EXPECT_EQ(snapshot.commands()[index].indexCount, 6u);
    }
    EXPECT_FLOAT_EQ(snapshot.commands()[6].clip.width, 100.0f);
    EXPECT_FLOAT_EQ(snapshot.commands()[7].clip.width, 20.0f);
    EXPECT_FLOAT_EQ(snapshot.commands()[8].clip.width, 100.0f);
}

TEST_F(UiPaintGlyphTests, NewerAppendOnlyPageUpgradesBindingWithoutMovingEarlierGlyphUvs){
    const SharedGlyphPage original = makePage();
    const SharedGlyphPage appended = makePage(11u, 2u);
    ASSERT_TRUE(original);
    ASSERT_TRUE(appended);
    const Rect earlierUv{ 0.0f, 0.0f, 0.25f, 0.25f };
    ASSERT_TRUE(m_builder.drawGlyph(original, { 1.0f, 1.0f, 4.0f, 4.0f }, earlierUv));
    ASSERT_TRUE(m_builder.prepareGlyphPages(&appended, 1u));
    ASSERT_TRUE(m_builder.drawGlyph(appended, { 6.0f, 1.0f, 4.0f, 4.0f }, { 0.5f, 0.5f, 0.25f, 0.25f }));
    // A later caller retaining the old page must neither downgrade the binding nor split the compatible batch.
    ASSERT_TRUE(m_builder.drawGlyph(original, { 11.0f, 1.0f, 4.0f, 4.0f }, earlierUv));
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.glyphPages().size(), 1u);
    ASSERT_EQ(snapshot.commands().size(), 1u);
    ASSERT_EQ(snapshot.vertices().size(), 12u);
    EXPECT_EQ(snapshot.commands()[0].indexCount, 18u);
    EXPECT_EQ(snapshot.commands()[0].glyphPageIndex, 0u);
    EXPECT_EQ(snapshot.glyphPages()[0].get(), appended.get());
    EXPECT_EQ(snapshot.glyphPages()[0]->binding().generation, 2u);
    EXPECT_EQ(snapshot.glyphPages()[0]->pixels()[0], original->pixels()[0]);
    EXPECT_EQ(snapshot.glyphPages()[0]->pixels()[32], 192u);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0].texCoord.x, 0.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0].texCoord.y, 0.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2].texCoord.x, 0.25f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2].texCoord.y, 0.25f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[4].texCoord.x, 0.5f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[4].texCoord.y, 0.5f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[8].texCoord.x, 0.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[10].texCoord.x, 0.25f);
}

TEST_F(UiPaintGlyphTests, FrozenSnapshotRetainsOldCoverageAfterBuilderReuseAndPageRelease){
    SharedGlyphPage original = makePage();
    ASSERT_TRUE(original);
    ASSERT_TRUE(m_builder.drawGlyph(original, { 1.0f, 1.0f, 4.0f, 4.0f }, { 0.0f, 0.0f, 0.25f, 0.25f }));
    const DrawSnapshot first = m_builder.freeze();
    original.reset();
    begin(8u);
    SharedGlyphPage appended = makePage(11u, 2u);
    ASSERT_TRUE(appended);
    ASSERT_TRUE(m_builder.drawGlyph(appended, { 6.0f, 1.0f, 4.0f, 4.0f }, { 0.5f, 0.5f, 0.25f, 0.25f }));
    const DrawSnapshot second = m_builder.freeze();
    appended.reset();
    begin(9u);
    ASSERT_EQ(first.glyphPages().size(), 1u);
    ASSERT_EQ(second.glyphPages().size(), 1u);
    EXPECT_EQ(first.glyphPages()[0]->binding().generation, 1u);
    EXPECT_EQ(second.glyphPages()[0]->binding().generation, 2u);
    EXPECT_NE(first.glyphPages()[0].get(), second.glyphPages()[0].get());
    EXPECT_EQ(first.glyphPages()[0]->pixels()[0], 64u);
    EXPECT_EQ(first.glyphPages()[0]->pixels()[32], 0u);
    EXPECT_EQ(second.glyphPages()[0]->pixels()[0], 64u);
    EXPECT_EQ(second.glyphPages()[0]->pixels()[32], 192u);
    EXPECT_FLOAT_EQ(first.vertices()[0].texCoord.x, 0.0f);
    EXPECT_FLOAT_EQ(second.vertices()[0].texCoord.x, 0.5f);
}

TEST_F(UiPaintGlyphTests, RejectedPageAdmissionAtCapacityPreservesBindingsAndGeometry){
    FixedVector<SharedGlyphPage, s_PaintMaxGlyphPages> pages;
    for(usize index = 0u; index < s_PaintMaxGlyphPages; ++index){
        SharedGlyphPage page = makePage(100u + index);
        ASSERT_TRUE(page);
        pages.push_back(Move(page));
    }
    ASSERT_TRUE(m_builder.drawGlyph(pages[0], { 2.0f, 3.0f, 4.0f, 5.0f }, { 0.125f, 0.25f, 0.5f, 0.5f }));
    ASSERT_TRUE(m_builder.prepareGlyphPages(pages.data(), pages.size()));
    const SharedGlyphPage upgrade = makePage(100u, 2u);
    const SharedGlyphPage overflow = makePage(1000u);
    ASSERT_TRUE(upgrade);
    ASSERT_TRUE(overflow);
    const Array<SharedGlyphPage, 2u> candidates{ upgrade, overflow };
    EXPECT_FALSE(m_builder.prepareGlyphPages(candidates.data(), candidates.size()));
    EXPECT_FALSE(m_builder.drawGlyph(overflow, { 10.0f, 10.0f, 4.0f, 4.0f }, { 0.0f, 0.0f, 0.5f, 0.5f }));
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.glyphPages().size(), s_PaintMaxGlyphPages);
    for(usize index = 0u; index < pages.size(); ++index)
        EXPECT_EQ(snapshot.glyphPages()[index].get(), pages[index].get());
    EXPECT_EQ(snapshot.glyphPages()[0]->binding().generation, 1u);
    EXPECT_EQ(snapshot.glyphPages()[0]->pixels()[32], 0u);
    ASSERT_EQ(snapshot.vertices().size(), 4u);
    ASSERT_EQ(snapshot.indices().size(), 6u);
    ASSERT_EQ(snapshot.commands().size(), 1u);
    EXPECT_EQ(snapshot.commands()[0].firstIndex, 0u);
    EXPECT_EQ(snapshot.commands()[0].indexCount, 6u);
    EXPECT_EQ(snapshot.commands()[0].glyphPageIndex, 0u);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0].position.x, 2.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0].position.y, 3.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2].position.x, 6.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2].position.y, 8.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0].texCoord.x, 0.125f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2].texCoord.y, 0.75f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

