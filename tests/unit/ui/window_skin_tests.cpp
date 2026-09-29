// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_window_skin_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;

class UiWindowSkinTests : public WidgetFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiWindowSkinTests, ReplacingAtlasChangesMetricsAndBindingsWithoutChangingWidgetIdentity){
    WindowState state;
    WindowOptions options;
    options.initialBounds = { 30.0f, 40.0f, 240.0f, 150.0f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Theme", state, options));
    EXPECT_FALSE(m_builder.button("apply", "Apply"));
    ASSERT_TRUE(finishWindow());
    const DrawSnapshot first = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(1u));
    const HitTarget* original = target(id("apply"));
    ASSERT_NE(original, nullptr);
    const HitTarget oldTarget = *original;
    EXPECT_FLOAT_EQ(oldTarget.rectangle.x, 33.0f);
    EXPECT_FLOAT_EQ(oldTarget.rectangle.y, 78.0f);
    EXPECT_EQ(m_context.input().hitTest({ 40.0f, 86.0f }), oldTarget.id);

    UiSkin::RegionVector regions(m_arena);
    regions.assign(m_skin.regions().begin(), m_skin.regions().end());
    for(UiSkinRegion& region : regions){
        region.rectangle.x += 128u;
        region.rectangle.y = 8u;
        if(region.name == Name("window.normal")){
            region.padding = { 11.0f, 12.0f, 13.0f, 14.0f };
            region.minimumWidth = 280.0f;
            region.minimumHeight = 200.0f;
        }
        else if(region.name == Name("window.title")){
            region.padding = { 7.0f, 9.0f, 11.0f, 13.0f };
            region.minimumHeight = 44.0f;
        }
        else if(region.name == Name("window.collapse")){
            region.minimumWidth = 26.0f;
            region.minimumHeight = 28.0f;
        }
        else if(region.name == Name("button.normal") || region.name == Name("button.hover")
            || region.name == Name("button.pressed") || region.name == Name("button.disabled")){
            region.padding = { 13.0f, 14.0f, 15.0f, 16.0f };
            region.minimumWidth = 120.0f;
            region.minimumHeight = 60.0f;
        }
    }
    regions.push_back({ Name("window.resize"), { 224u, 8u, 8u, 8u }, {}, {}, 18.0f, 22.0f, UiSkinDrawMode::Sprite });
    const Core::Assets::AssetRef<Texture> replacementTexture("tests/ui/alternate_texture");
    m_skin.setAtlas(replacementTexture, 256u, 32u, 2.0f, Move(regions));
    ASSERT_TRUE(m_skin.validatePayload());
    m_skinGeneration = 2u;
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginWindow("window", "Theme", state, options));
    EXPECT_FALSE(m_builder.button("apply", "Apply"));
    ASSERT_TRUE(finishWindow());
    const DrawSnapshot second = m_paint.freeze();
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_EQ(m_context.input().hitTest({ 40.0f, 86.0f }), oldTarget.id);
    ASSERT_TRUE(m_context.commitFrame(2u));
    const HitTarget* updated = target(id("apply"));
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->id, oldTarget.id);
    EXPECT_EQ(updated->declarationGeneration, oldTarget.declarationGeneration);
    EXPECT_FLOAT_EQ(updated->rectangle.x, 41.0f);
    EXPECT_FLOAT_EQ(updated->rectangle.y, 102.0f);
    EXPECT_FLOAT_EQ(updated->rectangle.width, 120.0f);
    EXPECT_FLOAT_EQ(updated->rectangle.height, 60.0f);
    EXPECT_NE(m_context.input().hitTest({ 40.0f, 86.0f }), updated->id);
    EXPECT_EQ(m_context.input().hitTest({ 55.0f, 115.0f }), updated->id);
    EXPECT_FLOAT_EQ(state.bounds.width, 280.0f);
    EXPECT_FLOAT_EQ(state.bounds.height, 200.0f);
    const HitTarget* resize = target(id("@window.resize"));
    ASSERT_NE(resize, nullptr);
    EXPECT_FLOAT_EQ(resize->rectangle.width, 22.0f);
    EXPECT_FLOAT_EQ(resize->rectangle.height, 22.0f);
    EXPECT_EQ(first.skinBinding().texture, Core::Assets::AssetRef<Texture>("tests/ui/texture"));
    EXPECT_EQ(first.skinBinding().generation, 1u);
    EXPECT_EQ(first.skinBinding().atlasWidth, 128u);
    EXPECT_EQ(second.skinBinding().texture, replacementTexture);
    EXPECT_EQ(second.skinBinding().generation, 2u);
    EXPECT_EQ(second.skinBinding().atlasWidth, 256u);
    EXPECT_FLOAT_EQ(second.skinBinding().referenceDensity, 2.0f);
    ASSERT_FALSE(first.commands().empty());
    ASSERT_FALSE(second.commands().empty());
    const Vertex& firstWindow = first.vertices()[first.indices()[first.commands().front().firstIndex]];
    const Vertex& secondWindow = second.vertices()[second.indices()[second.commands().front().firstIndex]];
    EXPECT_FLOAT_EQ(firstWindow.texCoord.x, 0.0f);
    EXPECT_FLOAT_EQ(firstWindow.texCoord.y, 0.0f);
    EXPECT_FLOAT_EQ(secondWindow.texCoord.x, 0.5f);
    EXPECT_FLOAT_EQ(secondWindow.texCoord.y, 0.25f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

