// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/paint.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_paint_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;


class UiPaintTests : public testing::Test{
public:
    UiPaintTests()
        : m_arena(Name("tests/ui/paint"))
        , m_skin(m_arena, Name("tests/ui/skin"))
        , m_builder(m_arena)
    {
        UiSkin::RegionVector regions(m_arena);
        regions.push_back({ Name("sprite"), { 16u, 32u, 32u, 24u }, {}, {}, 0.0f, 0.0f, UiSkinDrawMode::Sprite });
        UiSkinRegion panel{ Name("panel"), { 0u, 0u, 32u, 24u }, { 4u, 6u, 8u, 2u }, {}, 0.0f, 0.0f, UiSkinDrawMode::NineSlice };
        regions.push_back(panel);
        m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 128u, 64u, 2.0f, Move(regions));
        m_builder.begin({ 200.0f, 100.0f, 1.5f, 2.0f }, 7u, 3u, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
        m_builder.reserve(32u);
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    UiSkin m_skin;
    PaintBuilder m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPaintTests, CurrentClipCopiesTheActiveIntersectionAndRestoresAfterOverlay){
    m_builder.pushClip({ 10.0f, 10.0f, 30.0f, 30.0f });
    m_builder.pushClip({ 20.0f, 5.0f, 40.0f, 20.0f });
    const Rect nested = m_builder.currentClip();
    EXPECT_FLOAT_EQ(nested.x, 20.0f);
    EXPECT_FLOAT_EQ(nested.y, 10.0f);
    EXPECT_FLOAT_EQ(nested.width, 20.0f);
    EXPECT_FLOAT_EQ(nested.height, 15.0f);
    ASSERT_TRUE(m_builder.beginOverlay(1u));
    EXPECT_FLOAT_EQ(m_builder.currentClip().width, 200.0f);
    m_builder.pushClip({ 250.0f, 0.0f, 1.0f, 1.0f });
    EXPECT_FLOAT_EQ(m_builder.currentClip().width, 0.0f);
    ASSERT_TRUE(m_builder.popClip());
    ASSERT_TRUE(m_builder.endOverlay());
    EXPECT_FLOAT_EQ(m_builder.currentClip().x, nested.x);
    EXPECT_FLOAT_EQ(m_builder.currentClip().height, nested.height);
    ASSERT_TRUE(m_builder.popClip());
    ASSERT_TRUE(m_builder.popClip());
}

TEST_F(UiPaintTests, BoundaryClipsKeepEmptyExtentsAndSecondOperandSignedZeroOrigins){
    m_builder.pushClip({ 2.0f, 3.0f, 5.0f, 7.0f });
    for(const Rect boundary : { Rect{ 8.0f, 11.0f, 2.0f, 2.0f }, Rect{ 7.0f, 10.0f, 2.0f, 2.0f } }){
        m_builder.pushClip(boundary);
        const Rect clipped = m_builder.currentClip();
        EXPECT_FLOAT_EQ(clipped.x, boundary.x);
        EXPECT_FLOAT_EQ(clipped.y, boundary.y);
        EXPECT_FLOAT_EQ(clipped.width, 0.0f);
        EXPECT_FLOAT_EQ(clipped.height, 0.0f);
        ASSERT_TRUE(m_builder.popClip());
    }
    ASSERT_TRUE(m_builder.popClip());

    m_builder.pushClip({ -0.0f, -0.0f, 1.0f, 1.0f });
    m_builder.pushClip({ 0.0f, 0.0f, 1.0f, 1.0f });
    EXPECT_FALSE(SignBit(m_builder.currentClip().x));
    EXPECT_FALSE(SignBit(m_builder.currentClip().y));
    ASSERT_TRUE(m_builder.popClip());
    EXPECT_TRUE(SignBit(m_builder.currentClip().x));
    EXPECT_TRUE(SignBit(m_builder.currentClip().y));
    ASSERT_TRUE(m_builder.popClip());

    m_builder.pushClip({ 0.0f, 0.0f, 1.0f, 1.0f });
    m_builder.pushClip({ -0.0f, -0.0f, 1.0f, 1.0f });
    EXPECT_TRUE(SignBit(m_builder.currentClip().x));
    EXPECT_TRUE(SignBit(m_builder.currentClip().y));
    EXPECT_FLOAT_EQ(m_builder.currentClip().width, 1.0f);
    EXPECT_FLOAT_EQ(m_builder.currentClip().height, 1.0f);
}

TEST_F(UiPaintTests, NestedClipsTrimGeometryAndUvAndRestoreTheirParent){
    EXPECT_FALSE(m_builder.popClip());
    m_builder.pushClip({ 10.0f, 10.0f, 30.0f, 30.0f });
    m_builder.pushClip({ 20.0f, 5.0f, 40.0f, 20.0f });
    ASSERT_TRUE(m_builder.drawRegion(Name("sprite"), { 0.0f, 0.0f, 64.0f, 48.0f }));
    m_builder.pushClip({ 100.0f, 100.0f, 1.0f, 1.0f });
    m_builder.fillRect({ 0.0f, 0.0f, 200.0f, 100.0f });
    ASSERT_TRUE(m_builder.popClip());
    ASSERT_TRUE(m_builder.popClip());
    m_builder.fillRect({ 0.0f, 0.0f, 200.0f, 100.0f });
    ASSERT_TRUE(m_builder.popClip());
    const auto snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.vertices().size(), 8u);
    ASSERT_EQ(snapshot.commands().size(), 2u);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0].position.x, 20.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0].position.y, 10.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2].position.x, 40.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2].position.y, 25.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0].texCoord.x, 26.0f / 128.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0].texCoord.y, 37.0f / 64.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[4].position.x, 10.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[6].position.y, 40.0f);
}

TEST_F(UiPaintTests, UndersizedNineSliceCollapsesCenterAndScalesOpposingBorders){
    ASSERT_TRUE(m_builder.drawRegion(Name("panel"), { 10.0f, 20.0f, 3.0f, 2.0f }));
    const auto snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.vertices().size(), 16u);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2].position.x, 11.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2].position.y, 21.5f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[4].position.x, 11.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[6].position.x, 13.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[8].position.y, 21.5f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[14].position.y, 22.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2].texCoord.x, 4.0f / 128.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[4].texCoord.x, 24.0f / 128.0f);
}

TEST_F(UiPaintTests, BatchesOnlyMergeAdjacentCompatiblePaintAndKeepIndexOrder){
    m_builder.fillRect({ 1.0f, 1.0f, 5.0f, 5.0f }, { 1.0f, 0.0f, 0.0f, 0.5f });
    m_builder.fillRect({ 2.0f, 2.0f, 5.0f, 5.0f }, { 0.0f, 1.0f, 0.0f, 1.0f });
    ASSERT_TRUE(m_builder.drawRegion(Name("sprite"), { 3.0f, 3.0f, 5.0f, 5.0f }));
    m_builder.fillRect({ 4.0f, 4.0f, 5.0f, 5.0f });
    m_builder.pushClip({ 0.0f, 0.0f, 20.0f, 20.0f });
    m_builder.fillRect({ 5.0f, 5.0f, 5.0f, 5.0f });
    ASSERT_TRUE(m_builder.popClip());
    const auto snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.commands().size(), 4u);
    EXPECT_EQ(snapshot.commands()[0].indexCount, 12u);
    EXPECT_EQ(snapshot.commands()[1].material, PaintMaterial::Skin);
    EXPECT_EQ(snapshot.commands()[2].firstIndex, 18u);
    EXPECT_EQ(snapshot.commands()[3].firstIndex, 24u);
}

TEST_F(UiPaintTests, FrozenSnapshotOwnsPaintAcrossBuilderReuseAndSourceSkinChanges){
    ASSERT_TRUE(m_builder.drawRegion(Name("sprite"), { 0.0f, 0.0f, 20.0f, 20.0f }));
    EXPECT_FALSE(m_builder.drawRegion(Name("missing"), { 0.0f, 0.0f, 20.0f, 20.0f }));
    const auto first = m_builder.freeze();
    UiSkin::RegionVector regions(m_arena);
    regions.push_back({ Name("sprite"), { 0u, 0u, 4u, 4u }, {}, {}, 0.0f, 0.0f, UiSkinDrawMode::Sprite });
    m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/replacement"), 16u, 16u, 1.0f, Move(regions));
    m_builder.begin({ 20.0f, 20.0f }, 8u, 4u, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
    ASSERT_TRUE(m_builder.drawRegion(Name("sprite"), { 0.0f, 0.0f, 10.0f, 10.0f }));
    const auto second = m_builder.freeze();
    EXPECT_EQ(first.generation(), 7u);
    EXPECT_EQ(first.skinBinding().generation, 3u);
    EXPECT_EQ(first.skinBinding().texture.name(), Name("tests/ui/texture"));
    EXPECT_EQ(first.skinBinding().atlasWidth, 128u);
    EXPECT_FLOAT_EQ(first.displayMetrics().pixelScaleX, 1.5f);
    EXPECT_FLOAT_EQ(first.vertices()[0].texCoord.x, 16.0f / 128.0f);
    EXPECT_FLOAT_EQ(first.vertices()[2].position.x, 20.0f);
    EXPECT_EQ(second.skinBinding().generation, 4u);
    EXPECT_FLOAT_EQ(second.vertices()[0].texCoord.x, 0.0f);
    EXPECT_NE(first.vertices().data(), second.vertices().data());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

