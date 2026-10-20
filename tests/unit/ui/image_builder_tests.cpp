// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_image_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;

constexpr AStringView s_SpriteRegionName = "image.sprite";
constexpr AStringView s_PanelKey = "panel";
constexpr AStringView s_BeforeKey = "before";

class UiImageBuilderTests : public WidgetFixture{
protected:
    [[nodiscard]] static ImageOptions Fixed(const f32 width = 96.0f, const f32 height = 40.0f)noexcept{
        ImageOptions options;
        options.width = { LayoutSizePolicy::Fixed, width };
        options.height = { LayoutSizePolicy::Fixed, height };
        return options;
    }


protected:
    virtual void SetUp()override{
        WidgetFixture::SetUp();
        if(HasFatalFailure())
            return;
        configureImages();
    }

    void configureImages(){
        configureSkin();
        UiSkin::RegionVector regions(m_arena);
        for(const UiSkinRegion& region : m_skin.regions())
            regions.push_back(region);
        UiSkinRegion sprite;
        sprite.name = Name(s_SpriteRegionName);
        sprite.rectangle = { 0u, 16u, 20u, 12u };
        sprite.padding = { 9.0f, 9.0f, 9.0f, 9.0f };
        regions.push_back(Move(sprite));
        m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 128u, 32u, 1.0f, Move(regions));
        ASSERT_TRUE(m_skin.validatePayload());
        m_builder.setSkin(m_skin);
    }

    [[nodiscard]] bool panel(const u64 generation){
        return begin(generation) && m_builder.beginPanel(s_PanelKey, { 10.0f, 10.0f, 360.0f, 400.0f });
    }

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiImageBuilderTests, CallerMutationCannotChangeDeferredImageNameSizeOrStraightTint){
    ASSERT_TRUE(panel(1u));
    Name name(s_SpriteRegionName);
    ImageOptions options = Fixed(80.0f, 32.0f);
    options.tint = { 0.8f, 1.0f, 0.6f, 0.5f };
    ASSERT_TRUE(m_builder.image("image", name, options));
    name = Name("white");
    options = Fixed(20.0f, 20.0f);
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    const auto sprite = sampleSkinPaint(snapshot, Name(s_SpriteRegionName));
    ASSERT_TRUE(sprite);
    EXPECT_FLOAT_EQ(sprite->bounds.width, 80.0f);
    EXPECT_FLOAT_EQ(sprite->bounds.height, 32.0f);
    EXPECT_FLOAT_EQ(sprite->color.r, 0.4f);
    EXPECT_FLOAT_EQ(sprite->color.g, 0.5f);
    EXPECT_FLOAT_EQ(sprite->color.b, 0.3f);
    EXPECT_FLOAT_EQ(sprite->color.a, 0.5f);
}

TEST_F(UiImageBuilderTests, ExternalClipUpdatesSpriteUvsAndRestoresThePaintStack){
    ASSERT_TRUE(panel(1u));
    ASSERT_TRUE(m_builder.image("image", Name(s_SpriteRegionName), Fixed()));
    m_paint.pushClip({ 20.0f, 0.0f, 20.0f, 600.0f });
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_paint.popClip());
    m_paint.fillRect({ 60.0f, 80.0f, 8.0f, 8.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
    const DrawSnapshot snapshot = m_paint.freeze();
    const auto sprite = sampleSkinPaint(snapshot, Name(s_SpriteRegionName));
    ASSERT_TRUE(sprite);
    EXPECT_FLOAT_EQ(sprite->bounds.x, 20.0f);
    EXPECT_FLOAT_EQ(sprite->bounds.width, 20.0f);
    EXPECT_NEAR(sprite->maximumU, (27.0f / 96.0f) * (20.0f / 128.0f), 0.000001f);
    bool sentinel = false;
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material != PaintMaterial::Solid)
            continue;
        const Vertex& vertex = snapshot.vertices()[snapshot.indices()[command.firstIndex]];
        sentinel = sentinel || (vertex.position.x == 60.0f && vertex.position.y == 80.0f);
    }
    EXPECT_TRUE(sentinel);
}

TEST_F(UiImageBuilderTests, FullyTransparentAndEmptyImagesRemainValidWithoutQuads){
    ASSERT_TRUE(panel(1u));
    ImageOptions transparent = Fixed();
    transparent.tint.a = 0.0f;
    ASSERT_TRUE(m_builder.image("transparent", Name(s_SpriteRegionName), transparent));
    ASSERT_TRUE(m_builder.image("empty", Name(s_SpriteRegionName), Fixed(0.0f, 20.0f)));
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_FALSE(sampleSkinPaint(snapshot, Name(s_SpriteRegionName)));
}

TEST_F(UiImageBuilderTests, UnknownRegionRejectsWithoutReplacingAcceptedTargets){
    ASSERT_TRUE(panel(1u));
    EXPECT_FALSE(m_builder.button(s_BeforeKey, "Before"));
    ASSERT_TRUE(m_builder.image("image", Name(s_SpriteRegionName)));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId before = id(s_BeforeKey, s_PanelKey);
    ASSERT_NE(target(before), nullptr);
    const HitTarget accepted = *target(before);
    ASSERT_TRUE(panel(2u));
    EXPECT_FALSE(m_builder.image("image", Name("unknown.region")));
    EXPECT_FALSE(m_context.commitFrame(2u));
    ASSERT_NE(target(before), nullptr);
    EXPECT_EQ(target(before)->declarationGeneration, accepted.declarationGeneration);
}

TEST_F(UiImageBuilderTests, NonfiniteTintRejectsWithoutReplacingAcceptedTargets){
    ASSERT_TRUE(panel(1u));
    EXPECT_FALSE(m_builder.button(s_BeforeKey, "Before"));
    ASSERT_TRUE(m_builder.image("image", Name(s_SpriteRegionName)));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId before = id(s_BeforeKey, s_PanelKey);
    ASSERT_NE(target(before), nullptr);
    const HitTarget accepted = *target(before);
    ASSERT_TRUE(panel(2u));
    ImageOptions options;
    options.tint.r = BitCast<f32>(0x7fc00001u);
    EXPECT_FALSE(m_builder.image("image", Name(s_SpriteRegionName), options));
    EXPECT_FALSE(m_context.commitFrame(2u));
    ASSERT_NE(target(before), nullptr);
    EXPECT_EQ(target(before)->declarationGeneration, accepted.declarationGeneration);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

