// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_progress_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;

struct ProgressPaintSample{
    Rect bounds;
    Color color;
    f32 maximumU = 0.0f;
    usize quads = 0u;
};

class UiProgressBuilderTests : public WidgetFixture{
protected:
    [[nodiscard]] static ProgressOptions MakeOptions(){
        ProgressOptions options;
        options.width = { LayoutSizePolicy::Fixed, 240.0f };
        return options;
    }


protected:
    virtual void SetUp()override{
        WidgetFixture::SetUp();
        if(HasFatalFailure())
            return;
        configureProgress();
        m_builder.progressStyle().padding = { 4.0f, 4.0f, 4.0f, 4.0f };
    }

    void configureProgress(const bool preferred = true, const bool fallback = true){
        configureSkin();
        UiSkin::RegionVector regions(m_arena);
        for(const UiSkinRegion& region : m_skin.regions())
            regions.push_back(region);
        const Name names[]{ Name("progress.track"), Name("progress.fill"),
            Name("scrollbar.track"), Name("scrollbar.thumb.normal") };
        for(u32 index = 0u; index < 4u; ++index){
            if((index < 2u && !preferred) || (index >= 2u && !fallback))
                continue;
            UiSkinRegion region;
            region.name = names[index];
            region.rectangle = { 96u + index * 32u, 8u, 24u, 24u };
            region.sliceInsets = { 6u, 6u, 6u, 6u };
            region.minimumWidth = 12.0f;
            region.minimumHeight = 12.0f;
            region.drawMode = UiSkinDrawMode::NineSlice;
            regions.push_back(Move(region));
        }
        m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 256u, 32u, 1.0f, Move(regions));
        ASSERT_TRUE(m_skin.validatePayload());
        m_builder.setSkin(m_skin);
    }

    [[nodiscard]] bool panel(const u64 generation){
        return begin(generation) && m_builder.beginPanel("panel", { 10.0f, 10.0f, 360.0f, 400.0f });
    }

    [[nodiscard]] bool sample(const DrawSnapshot& snapshot, const Name& name, ProgressPaintSample& out)const{
        const UiSkinRegion* region = m_skin.findRegion(name);
        if(!region)
            return false;
        const f32 leftU = static_cast<f32>(region->rectangle.x) / m_skin.atlasWidth();
        const f32 rightU = static_cast<f32>(region->rectangle.x + region->rectangle.width) / m_skin.atlasWidth();
        const f32 topV = static_cast<f32>(region->rectangle.y) / m_skin.atlasHeight();
        const f32 bottomV = static_cast<f32>(region->rectangle.y + region->rectangle.height) / m_skin.atlasHeight();
        ProgressPaintSample candidate;
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


TEST_F(UiProgressBuilderTests, DeclarationCopiesFractionOptionsStyleAndResolvedNames){
    ASSERT_TRUE(panel(1u));
    f64 fraction = 0.25;
    ProgressOptions options = MakeOptions();
    m_builder.progressStyle().fillTint = { 0.8f, 0.6f, 0.4f, 0.5f };
    ASSERT_TRUE(m_builder.progress("amount", fraction, options));
    fraction = 0.75;
    options.width.value = 100.0f;
    options.height = 80.0f;
    ProgressStyle& style = m_builder.progressStyle();
    style.padding = { 12.0f, 12.0f, 12.0f, 12.0f };
    style.track = Name("missing.track");
    style.fill = Name("missing.fill");
    style.fillTint = {};
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    ProgressPaintSample track;
    ProgressPaintSample fill;
    ASSERT_TRUE(sample(snapshot, Name("progress.track"), track));
    ASSERT_TRUE(sample(snapshot, Name("progress.fill"), fill));
    EXPECT_FLOAT_EQ(track.bounds.width, 240.0f);
    EXPECT_FLOAT_EQ(track.bounds.height, 32.0f);
    EXPECT_FLOAT_EQ(fill.bounds.x - track.bounds.x, 4.0f);
    EXPECT_FLOAT_EQ(fill.bounds.width, 58.0f);
    EXPECT_FLOAT_EQ(fill.bounds.height, 24.0f);
    EXPECT_FLOAT_EQ(fill.color.r, 0.4f);
    EXPECT_FLOAT_EQ(fill.color.g, 0.3f);
    EXPECT_FLOAT_EQ(fill.color.b, 0.2f);
    EXPECT_FLOAT_EQ(fill.color.a, 0.5f);
    EXPECT_DOUBLE_EQ(fraction, 0.75);
}

TEST_F(UiProgressBuilderTests, ExplicitFallbackRegionsResolveBeforeDeferredPaint){
    configureProgress(false, true);
    ASSERT_TRUE(panel(1u));
    ASSERT_TRUE(m_builder.progress("amount", 0.5, MakeOptions()));
    m_builder.progressStyle().trackFallback = Name("missing.track");
    m_builder.progressStyle().fillFallback = Name("missing.fill");
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    ProgressPaintSample track;
    ProgressPaintSample fill;
    ASSERT_TRUE(sample(snapshot, Name("scrollbar.track"), track));
    ASSERT_TRUE(sample(snapshot, Name("scrollbar.thumb.normal"), fill));
    EXPECT_FLOAT_EQ(fill.bounds.width, 116.0f);
}

TEST_F(UiProgressBuilderTests, FiniteFractionsClampAtExactEmptyAndFullGeometry){
    const f64 fractions[]{ -4.0, 0.0, 1.0, 4.0 };
    for(u64 index = 0u; index < 4u; ++index){
        ASSERT_TRUE(panel(index + 1u));
        ASSERT_TRUE(m_builder.progress("amount", fractions[index], MakeOptions()));
        ASSERT_TRUE(finishPanel());
        const DrawSnapshot snapshot = m_paint.freeze();
        ProgressPaintSample track;
        ProgressPaintSample fill;
        ASSERT_TRUE(sample(snapshot, Name("progress.track"), track));
        EXPECT_EQ(sample(snapshot, Name("progress.fill"), fill), index >= 2u);
        if(index >= 2u){
            EXPECT_FLOAT_EQ(fill.bounds.x, track.bounds.x + 4.0f);
            EXPECT_FLOAT_EQ(fill.bounds.width, track.bounds.width - 8.0f);
            EXPECT_FLOAT_EQ(fill.bounds.height, track.bounds.height - 8.0f);
        }
        m_context.abandonFrame();
    }
}

TEST_F(UiProgressBuilderTests, TinyFillClipsNaturalBorderTexelsWithoutCompressingBothCaps){
    ASSERT_TRUE(panel(1u));
    ASSERT_TRUE(m_builder.progress("amount", 0.01, MakeOptions()));
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    ProgressPaintSample track;
    ProgressPaintSample fill;
    ASSERT_TRUE(sample(snapshot, Name("progress.track"), track));
    ASSERT_TRUE(sample(snapshot, Name("progress.fill"), fill));
    EXPECT_NEAR(fill.bounds.width, (track.bounds.width - 8.0f) * 0.01f, 0.000001f);
    EXPECT_LT(fill.maximumU, 134.0f / 256.0f);
    EXPECT_NEAR(fill.maximumU, (128.0f + fill.bounds.width) / 256.0f, 0.000001f);
    EXPECT_EQ(fill.quads, 3u);
}

TEST_F(UiProgressBuilderTests, SubresolutionPositiveFractionRemainsAValidEmptyFill){
    ASSERT_TRUE(panel(1u));
    ASSERT_TRUE(m_builder.progress("amount", BitCast<f64>(1ull), MakeOptions()));
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    ProgressPaintSample sampleOut;
    EXPECT_TRUE(sample(snapshot, Name("progress.track"), sampleOut));
    EXPECT_FALSE(sample(snapshot, Name("progress.fill"), sampleOut));
}

TEST_F(UiProgressBuilderTests, ExternalClipConstrainsPaintAndBothNestedClipsAreRestored){
    ASSERT_TRUE(panel(1u));
    ASSERT_TRUE(m_builder.progress("amount", 0.25, MakeOptions()));
    m_paint.pushClip({ 20.0f, 0.0f, 20.0f, 600.0f });
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_paint.popClip());
    m_paint.fillRect({ 60.0f, 80.0f, 8.0f, 8.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
    const DrawSnapshot snapshot = m_paint.freeze();
    ProgressPaintSample fill;
    ASSERT_TRUE(sample(snapshot, Name("progress.fill"), fill));
    EXPECT_FLOAT_EQ(fill.bounds.x, 20.0f);
    EXPECT_FLOAT_EQ(fill.bounds.width, 20.0f);
    bool sentinel = false;
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material != PaintMaterial::Solid)
            continue;
        const Vertex& vertex = snapshot.vertices()[snapshot.indices()[command.firstIndex]];
        sentinel = sentinel || (vertex.position.x == 60.0f && vertex.position.y == 80.0f);
    }
    EXPECT_TRUE(sentinel);
}

TEST_F(UiProgressBuilderTests, InvalidFractionLeavesTheAcceptedTargetsAvailable){
    ASSERT_TRUE(panel(1u));
    EXPECT_FALSE(m_builder.button("before", "Before"));
    ASSERT_TRUE(m_builder.progress("amount", 0.5, MakeOptions()));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId before = id("before", "panel");
    ASSERT_NE(target(before), nullptr);
    const HitTarget accepted = *target(before);
    const usize count = m_context.input().targets().size();
    ASSERT_TRUE(panel(2u));
    EXPECT_FALSE(m_builder.progress("amount", BitCast<f64>(0x7ff8000000000001ull), MakeOptions()));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    ASSERT_NE(target(before), nullptr);
    EXPECT_EQ(target(before)->declarationGeneration, accepted.declarationGeneration);
    EXPECT_FLOAT_EQ(target(before)->rectangle.x, accepted.rectangle.x);
    EXPECT_FLOAT_EQ(target(before)->rectangle.width, accepted.rectangle.width);
    EXPECT_EQ(m_context.input().targets().size(), count);
}

TEST_F(UiProgressBuilderTests, MissingPreferredAndFallbackRegionsRejectTheCandidate){
    configureProgress(false, false);
    ASSERT_TRUE(panel(1u));
    EXPECT_FALSE(m_builder.progress("amount", 0.5, MakeOptions()));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_FALSE(m_context.commitFrame(1u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

