// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ui/paint.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_paint_overlay_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;


class UiPaintOverlayTests : public testing::Test{
public:
    UiPaintOverlayTests()
        : m_arena(Name("tests/ui/paint_overlay"))
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

    [[nodiscard]] SharedGlyphPage makeCoverage(){
        GlyphPage::Pixels pixels(m_arena);
        pixels.resize(4u, 128u);
        const GlyphPageBinding binding{
            .font = Core::Assets::AssetRef<Font>("tests/ui/font"),
            .fontGeneration = 9u,
            .atlasIdentity = 11u,
            .generation = 1u,
            .width = 2u,
            .height = 2u,
        };
        return CreateGlyphPage(m_arena, binding, Move(pixels));
    }

    [[nodiscard]] SharedSdfAtlasPage makeDistance(){
        SdfAtlasPage::Pixels pixels(m_arena);
        pixels.resize(4u * 4u * 4u, 128u);
        const SdfAtlasPageBinding binding{
            .font = Core::Assets::AssetRef<Font>("tests/ui/font"),
            .fontSha256 = {},
            .pixelsSha256 = {},
            .fontGeneration = 9u,
            .atlasIdentity = 22u,
            .generation = 1u,
            .width = 4u,
            .height = 4u,
            .spreadPixels = 2u,
        };
        return CreateSdfAtlasPage(m_arena, binding, Move(pixels));
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    UiSkin m_skin;
    PaintBuilder m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPaintOverlayTests, OverlayRendersAboveLaterBasePaintWithoutMergingItsBatch){
    const Rect rectangle{ 1.0f, 1.0f, 4.0f, 4.0f };
    m_builder.fillRect(rectangle, { 1.0f, 0.0f, 0.0f, 1.0f });
    ASSERT_TRUE(m_builder.beginOverlay(3u));
    m_builder.fillRect(rectangle, { 0.0f, 1.0f, 0.0f, 1.0f });
    m_builder.fillRect(rectangle, { 0.0f, 1.0f, 0.0f, 0.5f });
    ASSERT_TRUE(m_builder.endOverlay());
    m_builder.fillRect(rectangle, { 0.0f, 0.0f, 1.0f, 1.0f });
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.commands().size(), 3u);
    EXPECT_EQ(snapshot.commands()[0u].layer, 0u);
    EXPECT_EQ(snapshot.commands()[0u].firstIndex, 0u);
    EXPECT_EQ(snapshot.commands()[1u].layer, 0u);
    EXPECT_EQ(snapshot.commands()[1u].firstIndex, 18u);
    EXPECT_EQ(snapshot.commands()[2u].layer, 3u);
    EXPECT_EQ(snapshot.commands()[2u].firstIndex, 6u);
    EXPECT_EQ(snapshot.commands()[2u].indexCount, 12u);
    ASSERT_EQ(snapshot.indices().size(), 24u);
    for(u32 quad = 0u; quad < 4u; ++quad){
        EXPECT_EQ(snapshot.indices()[quad * 6u], quad * 4u);
        EXPECT_EQ(snapshot.indices()[quad * 6u + 5u], quad * 4u + 3u);
    }
}

TEST_F(UiPaintOverlayTests, LayerOrderPrecedesDeclarationOrderAndEqualLayersKeepTheirIndexOrder){
    const Rect rectangle{ 1.0f, 1.0f, 4.0f, 4.0f };
    ASSERT_TRUE(m_builder.beginOverlay(7u));
    m_builder.fillRect(rectangle, { 1.0f, 0.0f, 0.0f, 1.0f });
    ASSERT_TRUE(m_builder.endOverlay());
    ASSERT_TRUE(m_builder.beginOverlay(2u));
    m_builder.fillRect(rectangle, { 0.0f, 1.0f, 0.0f, 1.0f });
    ASSERT_TRUE(m_builder.endOverlay());
    ASSERT_TRUE(m_builder.beginOverlay(7u));
    m_builder.fillRect(rectangle, { 0.0f, 0.0f, 1.0f, 1.0f });
    ASSERT_TRUE(m_builder.endOverlay());
    m_builder.fillRect(rectangle);
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.commands().size(), 4u);
    EXPECT_EQ(snapshot.commands()[0u].layer, 0u);
    EXPECT_EQ(snapshot.commands()[0u].firstIndex, 18u);
    EXPECT_EQ(snapshot.commands()[1u].layer, 2u);
    EXPECT_EQ(snapshot.commands()[1u].firstIndex, 6u);
    EXPECT_EQ(snapshot.commands()[2u].layer, 7u);
    EXPECT_EQ(snapshot.commands()[2u].firstIndex, 0u);
    EXPECT_EQ(snapshot.commands()[3u].layer, 7u);
    EXPECT_EQ(snapshot.commands()[3u].firstIndex, 12u);
}

TEST_F(UiPaintOverlayTests, OverlayEscapesParentClipAndRestoresItsExactPriorClip){
    const Rect rectangle{ 0.0f, 0.0f, 100.0f, 80.0f };
    m_builder.pushClip({ 10.0f, 10.0f, 20.0f, 20.0f });
    m_builder.fillRect(rectangle);
    ASSERT_TRUE(m_builder.beginOverlay(4u));
    m_builder.pushClip({ 25.0f, 5.0f, 40.0f, 20.0f });
    m_builder.fillRect(rectangle);
    ASSERT_TRUE(m_builder.popClip());
    EXPECT_FALSE(m_builder.popClip());
    ASSERT_TRUE(m_builder.endOverlay());
    m_builder.fillRect(rectangle);
    ASSERT_TRUE(m_builder.popClip());
    m_builder.fillRect(rectangle);
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.vertices().size(), 16u);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.x, 10.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.y, 10.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2u].position.x, 30.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2u].position.y, 30.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[4u].position.x, 25.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[4u].position.y, 5.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[6u].position.x, 65.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[6u].position.y, 25.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[8u].position.x, 10.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[10u].position.y, 30.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[12u].position.x, 0.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[14u].position.y, 80.0f);
    ASSERT_EQ(snapshot.commands().size(), 4u);
    EXPECT_EQ(snapshot.commands()[3u].layer, 4u);
    EXPECT_FLOAT_EQ(snapshot.commands()[3u].clip.x, 25.0f);
    EXPECT_FLOAT_EQ(snapshot.commands()[3u].clip.y, 5.0f);
    EXPECT_FLOAT_EQ(snapshot.commands()[3u].clip.width, 40.0f);
    EXPECT_FLOAT_EQ(snapshot.commands()[3u].clip.height, 20.0f);
}

TEST_F(UiPaintOverlayTests, EscapingTheParentStillClipsOverlayGeometryAndUvsToTheLogicalViewport){
    m_builder.pushClip({ 50.0f, 50.0f, 5.0f, 5.0f });
    ASSERT_TRUE(m_builder.beginOverlay(2u));
    ASSERT_TRUE(m_builder.drawRegion(Name("sprite"), { -20.0f, -10.0f, 160.0f, 100.0f }));
    ASSERT_TRUE(m_builder.endOverlay());
    ASSERT_TRUE(m_builder.popClip());
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.commands().size(), 1u);
    ASSERT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_EQ(snapshot.commands()[0u].layer, 2u);
    EXPECT_FLOAT_EQ(snapshot.commands()[0u].clip.width, 100.0f);
    EXPECT_FLOAT_EQ(snapshot.commands()[0u].clip.height, 80.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.x, 0.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.y, 0.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2u].position.x, 100.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2u].position.y, 80.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].texCoord.x, 0.0625f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].texCoord.y, 0.05f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2u].texCoord.x, 0.375f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2u].texCoord.y, 0.45f);
}

TEST_F(UiPaintOverlayTests, RejectedAdmissionAndUnbalancedClosurePreserveTheActiveLayerAndClips){
    const Rect rectangle{ 0.0f, 0.0f, 100.0f, 80.0f };
    m_builder.pushClip({ 5.0f, 5.0f, 10.0f, 10.0f });
    EXPECT_FALSE(m_builder.beginOverlay(0u));
    EXPECT_FALSE(m_builder.endOverlay());
    m_builder.fillRect(rectangle);
    ASSERT_TRUE(m_builder.beginOverlay(8u));
    ASSERT_TRUE(m_builder.beginOverlay(9u));
    ASSERT_TRUE(m_builder.endOverlay());
    EXPECT_FALSE(m_builder.beginOverlay(0u));
    m_builder.pushClip({ 10.0f, 10.0f, 20.0f, 20.0f });
    EXPECT_FALSE(m_builder.endOverlay());
    m_builder.fillRect(rectangle);
    ASSERT_TRUE(m_builder.popClip());
    EXPECT_FALSE(m_builder.popClip());
    ASSERT_TRUE(m_builder.endOverlay());
    EXPECT_FALSE(m_builder.endOverlay());
    m_builder.fillRect(rectangle);
    ASSERT_TRUE(m_builder.popClip());
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.commands().size(), 3u);
    EXPECT_EQ(snapshot.commands()[0u].layer, 0u);
    EXPECT_EQ(snapshot.commands()[1u].layer, 0u);
    EXPECT_EQ(snapshot.commands()[2u].layer, 8u);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.x, 5.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2u].position.x, 15.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[4u].position.x, 10.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[6u].position.x, 30.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[8u].position.x, 5.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[10u].position.x, 15.0f);
}

TEST_F(UiPaintOverlayTests, NestedOverlayRestoresParentLayerAndItsClipAfterEscapingBoth){
    const Rect rectangle{ 0.0f, 0.0f, 100.0f, 80.0f };
    ASSERT_TRUE(m_builder.beginOverlay(2u));
    m_builder.pushClip({ 5.0f, 5.0f, 10.0f, 10.0f });
    m_builder.fillRect(rectangle);
    ASSERT_TRUE(m_builder.beginOverlay(3u));
    m_builder.fillRect(rectangle);
    EXPECT_FALSE(m_builder.popClip());
    ASSERT_TRUE(m_builder.endOverlay());
    m_builder.fillRect(rectangle);
    ASSERT_TRUE(m_builder.popClip());
    ASSERT_TRUE(m_builder.endOverlay());
    m_builder.fillRect(rectangle);
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.commands().size(), 4u);
    EXPECT_EQ(snapshot.commands()[0u].layer, 0u);
    EXPECT_EQ(snapshot.commands()[1u].layer, 2u);
    EXPECT_EQ(snapshot.commands()[2u].layer, 2u);
    EXPECT_EQ(snapshot.commands()[3u].layer, 3u);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.x, 5.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[4u].position.x, 0.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[6u].position.x, 100.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[8u].position.x, 5.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[10u].position.x, 15.0f);
}

TEST_F(UiPaintOverlayTests, BoundedOverlayDepthRejectsWithoutChangingTheLastAcceptedScope){
    for(u32 layer = 1u; layer <= s_PaintMaxOverlayDepth; ++layer)
        ASSERT_TRUE(m_builder.beginOverlay(layer));
    EXPECT_FALSE(m_builder.beginOverlay(99u));
    m_builder.fillRect({ 0.0f, 0.0f, 100.0f, 80.0f });
    for(usize depth = 0u; depth < s_PaintMaxOverlayDepth; ++depth)
        ASSERT_TRUE(m_builder.endOverlay());
    EXPECT_FALSE(m_builder.endOverlay());
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.commands().size(), 1u);
    EXPECT_EQ(snapshot.commands()[0u].layer, s_PaintMaxOverlayDepth);
}

TEST_F(UiPaintOverlayTests, UnrecordedScopesAreRejectedAndBeginResetsAbandonedOverlayState){
    PaintBuilder idle(m_arena);
    EXPECT_FALSE(idle.beginOverlay(1u));
    EXPECT_FALSE(idle.endOverlay());
    ASSERT_TRUE(m_builder.beginOverlay(5u));
    m_builder.pushClip({ 10.0f, 10.0f, 20.0f, 20.0f });
    m_builder.fillRect({ 10.0f, 10.0f, 10.0f, 10.0f });
    begin(8u);
    EXPECT_FALSE(m_builder.endOverlay());
    m_builder.fillRect({ 0.0f, 0.0f, 100.0f, 80.0f });
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.commands().size(), 1u);
    EXPECT_EQ(snapshot.generation(), 8u);
    EXPECT_EQ(snapshot.commands()[0u].layer, 0u);
    EXPECT_EQ(snapshot.commands()[0u].firstIndex, 0u);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.x, 0.0f);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2u].position.y, 80.0f);
    EXPECT_FALSE(m_builder.beginOverlay(1u));
    EXPECT_FALSE(m_builder.endOverlay());
}

TEST_F(UiPaintOverlayTests, SortingCommandsRetainsSkinCoverageDistanceBindingsAndUnchangedIndexRanges){
    const SharedGlyphPage coverage = makeCoverage();
    const SharedSdfAtlasPage distance = makeDistance();
    ASSERT_TRUE(coverage && distance);
    const Rect rectangle{ 1.0f, 1.0f, 4.0f, 4.0f };
    const Rect uv{ 0.0f, 0.0f, 0.5f, 0.5f };
    ASSERT_TRUE(m_builder.beginOverlay(4u));
    ASSERT_TRUE(m_builder.drawSdfGlyph(distance, 3u, rectangle, uv));
    ASSERT_TRUE(m_builder.drawGlyph(coverage, rectangle, uv));
    ASSERT_TRUE(m_builder.endOverlay());
    ASSERT_TRUE(m_builder.drawRegion(Name("sprite"), rectangle));
    ASSERT_TRUE(m_builder.drawSdfGlyph(distance, 1u, rectangle, uv));
    ASSERT_TRUE(m_builder.drawGlyph(coverage, rectangle, uv));
    const DrawSnapshot snapshot = m_builder.freeze();
    ASSERT_EQ(snapshot.commands().size(), 5u);
    ASSERT_EQ(snapshot.sdfPages().size(), 1u);
    ASSERT_EQ(snapshot.glyphPages().size(), 1u);
    EXPECT_EQ(snapshot.skinBinding().texture.name(), Name("tests/ui/texture"));
    EXPECT_EQ(snapshot.sdfPages()[0u].get(), distance.get());
    EXPECT_EQ(snapshot.glyphPages()[0u].get(), coverage.get());
    EXPECT_EQ(snapshot.commands()[0u].material, PaintMaterial::Skin);
    EXPECT_EQ(snapshot.commands()[0u].firstIndex, 12u);
    EXPECT_EQ(snapshot.commands()[1u].material, PaintMaterial::SdfGlyph);
    EXPECT_EQ(snapshot.commands()[1u].sdfPageIndex, 0u);
    EXPECT_EQ(snapshot.commands()[1u].sdfChannel, 1u);
    EXPECT_EQ(snapshot.commands()[1u].firstIndex, 18u);
    EXPECT_EQ(snapshot.commands()[2u].material, PaintMaterial::Glyph);
    EXPECT_EQ(snapshot.commands()[2u].glyphPageIndex, 0u);
    EXPECT_EQ(snapshot.commands()[2u].firstIndex, 24u);
    EXPECT_EQ(snapshot.commands()[3u].layer, 4u);
    EXPECT_EQ(snapshot.commands()[3u].material, PaintMaterial::SdfGlyph);
    EXPECT_EQ(snapshot.commands()[3u].sdfPageIndex, 0u);
    EXPECT_EQ(snapshot.commands()[3u].sdfChannel, 3u);
    EXPECT_EQ(snapshot.commands()[3u].firstIndex, 0u);
    EXPECT_EQ(snapshot.commands()[4u].layer, 4u);
    EXPECT_EQ(snapshot.commands()[4u].material, PaintMaterial::Glyph);
    EXPECT_EQ(snapshot.commands()[4u].glyphPageIndex, 0u);
    EXPECT_EQ(snapshot.commands()[4u].firstIndex, 6u);
    ASSERT_EQ(snapshot.indices().size(), 30u);
    for(u32 quad = 0u; quad < 5u; ++quad){
        EXPECT_EQ(snapshot.indices()[quad * 6u], quad * 4u);
        EXPECT_EQ(snapshot.indices()[quad * 6u + 2u], quad * 4u + 2u);
        EXPECT_EQ(snapshot.indices()[quad * 6u + 5u], quad * 4u + 3u);
    }
    for(const DrawCommand& command : snapshot.commands()){
        EXPECT_EQ(command.indexCount, 6u);
        EXPECT_LT(command.firstIndex + command.indexCount - 1u, snapshot.indices().size());
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

