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

struct ImagePaintSample{
    Rect bounds;
    Color color;
    f32 firstQuadWidth = 0.0f;
    f32 maximumU = 0.0f;
    usize quads = 0u;
};

class UiImageBuilderTests : public WidgetFixture{
protected:
    [[nodiscard]] static ImageOptions Fixed(const f32 width = 96.0f, const f32 height = 40.0f){
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

    void configureImages(const f32 density = 1.0f){
        configureSkin();
        UiSkin::RegionVector regions(m_arena);
        for(const UiSkinRegion& region : m_skin.regions())
            regions.push_back(region);
        UiSkinRegion sprite;
        sprite.name = Name("image.sprite");
        sprite.rectangle = { 0u, 16u, 20u, 12u };
        sprite.padding = { 9.0f, 9.0f, 9.0f, 9.0f };
        regions.push_back(Move(sprite));
        UiSkinRegion slice;
        slice.name = Name("image.slice");
        slice.rectangle = { 96u, 8u, 24u, 24u };
        slice.sliceInsets = { 6u, 6u, 6u, 6u };
        slice.minimumWidth = 12.0f;
        slice.minimumHeight = 12.0f;
        slice.padding = { 9.0f, 9.0f, 9.0f, 9.0f };
        slice.drawMode = UiSkinDrawMode::NineSlice;
        regions.push_back(Move(slice));
        m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 128u, 32u, density, Move(regions));
        ASSERT_TRUE(m_skin.validatePayload());
        m_builder.setSkin(m_skin);
    }

    [[nodiscard]] bool panel(const u64 generation){
        return begin(generation) && m_builder.beginPanel("panel", { 10.0f, 10.0f, 360.0f, 400.0f });
    }

    [[nodiscard]] bool sample(const DrawSnapshot& snapshot, const Name& name, ImagePaintSample& out)const{
        const UiSkinRegion* region = m_skin.findRegion(name);
        if(!region)
            return false;
        const f32 leftU = static_cast<f32>(region->rectangle.x) / m_skin.atlasWidth();
        const f32 rightU = static_cast<f32>(region->rectangle.x + region->rectangle.width) / m_skin.atlasWidth();
        const f32 topV = static_cast<f32>(region->rectangle.y) / m_skin.atlasHeight();
        const f32 bottomV = static_cast<f32>(region->rectangle.y + region->rectangle.height) / m_skin.atlasHeight();
        ImagePaintSample candidate;
        f32 right = 0.0f;
        f32 bottom = 0.0f;
        for(const DrawCommand& command : snapshot.commands()){
            if(command.material != PaintMaterial::Skin)
                continue;
            for(u32 index = command.firstIndex; index + 5u < command.firstIndex + command.indexCount; index += 6u){
                const Vertex& first = snapshot.vertices()[snapshot.indices()[index]];
                const Vertex& opposite = snapshot.vertices()[snapshot.indices()[index + 2u]];
                if(
                    first.texCoord.x < leftU || opposite.texCoord.x > rightU
                    || first.texCoord.y < topV || opposite.texCoord.y > bottomV
                )
                    continue;
                if(candidate.quads == 0u){
                    candidate.bounds.x = first.position.x;
                    candidate.bounds.y = first.position.y;
                    candidate.color = first.color;
                    candidate.firstQuadWidth = opposite.position.x - first.position.x;
                    right = opposite.position.x;
                    bottom = opposite.position.y;
                }
                candidate.bounds.x = Min(candidate.bounds.x, first.position.x);
                candidate.bounds.y = Min(candidate.bounds.y, first.position.y);
                right = Max(right, opposite.position.x);
                bottom = Max(bottom, opposite.position.y);
                candidate.maximumU = Max(candidate.maximumU, opposite.texCoord.x);
                ++candidate.quads;
            }
        }
        if(candidate.quads == 0u)
            return false;
        candidate.bounds.width = right - candidate.bounds.x;
        candidate.bounds.height = bottom - candidate.bounds.y;
        out = candidate;
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiImageBuilderTests, DeclarationCopiesNameSizeAndStraightTintBeforePaint){
    ASSERT_TRUE(panel(1u));
    Name name("image.sprite");
    ImageOptions options = Fixed(80.0f, 32.0f);
    options.tint = { 0.8f, 1.0f, 0.6f, 0.5f };
    ASSERT_TRUE(m_builder.image("image", name, options));
    name = Name("white");
    options = Fixed(20.0f, 20.0f);
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    ImagePaintSample sprite;
    ASSERT_TRUE(sample(snapshot, Name("image.sprite"), sprite));
    EXPECT_FLOAT_EQ(sprite.bounds.width, 80.0f);
    EXPECT_FLOAT_EQ(sprite.bounds.height, 32.0f);
    EXPECT_FLOAT_EQ(sprite.color.r, 0.4f);
    EXPECT_FLOAT_EQ(sprite.color.g, 0.5f);
    EXPECT_FLOAT_EQ(sprite.color.b, 0.3f);
    EXPECT_FLOAT_EQ(sprite.color.a, 0.5f);
}

TEST_F(UiImageBuilderTests, ExternalClipUpdatesSpriteUvsAndRestoresThePaintStack){
    ASSERT_TRUE(panel(1u));
    ASSERT_TRUE(m_builder.image("image", Name("image.sprite"), Fixed()));
    m_paint.pushClip({ 20.0f, 0.0f, 20.0f, 600.0f });
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_paint.popClip());
    m_paint.fillRect({ 60.0f, 80.0f, 8.0f, 8.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
    const DrawSnapshot snapshot = m_paint.freeze();
    ImagePaintSample sprite;
    ASSERT_TRUE(sample(snapshot, Name("image.sprite"), sprite));
    EXPECT_FLOAT_EQ(sprite.bounds.x, 20.0f);
    EXPECT_FLOAT_EQ(sprite.bounds.width, 20.0f);
    EXPECT_NEAR(sprite.maximumU, (27.0f / 96.0f) * (20.0f / 128.0f), 0.000001f);
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
    ASSERT_TRUE(m_builder.image("transparent", Name("image.sprite"), transparent));
    ASSERT_TRUE(m_builder.image("empty", Name("image.sprite"), Fixed(0.0f, 20.0f)));
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    ImagePaintSample sprite;
    EXPECT_FALSE(sample(snapshot, Name("image.sprite"), sprite));
}

TEST_F(UiImageBuilderTests, UnknownRegionRejectsWithoutReplacingAcceptedTargets){
    ASSERT_TRUE(panel(1u));
    EXPECT_FALSE(m_builder.button("before", "Before"));
    ASSERT_TRUE(m_builder.image("image", Name("image.sprite")));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId before = id("before", "panel");
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
    EXPECT_FALSE(m_builder.button("before", "Before"));
    ASSERT_TRUE(m_builder.image("image", Name("image.sprite")));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId before = id("before", "panel");
    ASSERT_NE(target(before), nullptr);
    const HitTarget accepted = *target(before);
    ASSERT_TRUE(panel(2u));
    ImageOptions options;
    options.tint.r = BitCast<f32>(0x7fc00001u);
    EXPECT_FALSE(m_builder.image("image", Name("image.sprite"), options));
    EXPECT_FALSE(m_context.commitFrame(2u));
    ASSERT_NE(target(before), nullptr);
    EXPECT_EQ(target(before)->declarationGeneration, accepted.declarationGeneration);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

