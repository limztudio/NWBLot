// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/label.h>

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_region_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;

static constexpr StringView s_OverflowText = "MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM\nMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM\nMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static usize GlyphIndexCount(const DrawSnapshot& snapshot)noexcept{
    usize count = 0u;
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material == PaintMaterial::Glyph || command.material == PaintMaterial::SdfGlyph)
            count += command.indexCount;
    }
    return count;
}

static void ExpectGlyphRegion(const DrawSnapshot& snapshot, const Rect& expected){
    usize count = 0u;
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material != PaintMaterial::Glyph && command.material != PaintMaterial::SdfGlyph)
            continue;
        ExpectRect(command.clip, expected);
        ASSERT_LE(static_cast<usize>(command.firstIndex) + command.indexCount, snapshot.indices().size());
        for(u32 index = command.firstIndex; index < command.firstIndex + command.indexCount; ++index){
            ASSERT_LT(snapshot.indices()[index], snapshot.vertices().size());
            const Point& position = snapshot.vertices()[snapshot.indices()[index]].position;
            EXPECT_GE(position.x, expected.x);
            EXPECT_LE(position.x, expected.x + expected.width);
            EXPECT_GE(position.y, expected.y);
            EXPECT_LE(position.y, expected.y + expected.height);
        }
        count += command.indexCount;
    }
    EXPECT_GT(count, 0u);
}

static void CopyGlyphVertices(const DrawSnapshot& snapshot, Vector<Vertex, Core::Alloc::ScratchArena>& vertices){
    vertices.reserve(snapshot.indices().size());
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material != PaintMaterial::Glyph && command.material != PaintMaterial::SdfGlyph)
            continue;
        for(u32 index = command.firstIndex; index < command.firstIndex + command.indexCount; ++index)
            vertices.push_back(snapshot.vertices()[snapshot.indices()[index]]);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiTextRegionTests : public WidgetFixture{
protected:
    virtual void SetUp()override{
        WidgetFixture::SetUp();
        if(HasFatalFailure())
            return;
        UiSkin::RegionVector regions(m_arena);
        regions.assign(m_skin.regions().begin(), m_skin.regions().end());
        const AStringView names[]{ "checkbox.normal", "checkbox.hover", "checkbox.checked", "checkbox.disabled" };
        for(u32 index = 0u; index < LengthOf(names); ++index)
            regions.push_back({ Name(names[index]), { 88u + index * 8u, 0u, 8u, 8u }, {}, {}, 24.0f, 24.0f });
        m_skin.setAtlas(m_skin.texture(), m_skin.atlasWidth(), m_skin.atlasHeight(), m_skin.referenceDensity(), Move(regions));
        ASSERT_TRUE(m_skin.validatePayload());
        m_builder.setSkin(m_skin);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiTextRegionTests, StandaloneMultilineLabelClipsNestedBoundsAfterResizeAcrossDisplayScales){
    Label label(m_arena);
    ASSERT_EQ(label.setText({ .text = s_OverflowText, .fontSize = 16.0f }), TextLayoutStatus::Success);
    u64 generation = 1u;
    for(const f32 scale : { 1.0f, 1.5f, 2.0f }){
        usize previousIndices = 0u;
        for(const Rect& bounds : { Rect{ 20.0f, 20.0f, 80.0f, 34.0f }, Rect{ 20.0f, 20.0f, 41.0f, 19.0f } }){
            m_paint.begin({ 300.0f, 200.0f, scale, scale }, generation++, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
            m_paint.pushClip({ 12.0f, 10.0f, 210.0f, 140.0f });
            const Rect inherited{ 25.0f, 24.0f, 110.0f, 70.0f };
            m_paint.pushClip(inherited);
            ASSERT_TRUE(label.paint(m_text, m_paint, bounds));
            EXPECT_GT(label.layout().measure().x, bounds.width);
            EXPECT_GT(label.layout().measure().y, bounds.height);
            ExpectRect(m_paint.currentClip(), inherited);
            ASSERT_TRUE(m_paint.popClip());
            ASSERT_TRUE(m_paint.popClip());
            EXPECT_FALSE(m_paint.popClip());
            const DrawSnapshot snapshot = m_paint.freeze();
            ExpectGlyphRegion(snapshot, { 25.0f, 24.0f, bounds.width - 5.0f, bounds.height - 4.0f });
            const usize indices = GlyphIndexCount(snapshot);
            if(previousIndices != 0u)
                EXPECT_LT(indices, previousIndices);
            previousIndices = indices;
        }
    }
}

TEST_F(UiTextRegionTests, EmptyAndFailedStandaloneLabelRegionsRestoreClipBeforePaintingASibling){
    Label label(m_arena);
    ASSERT_EQ(label.setText({ .text = s_OverflowText }), TextLayoutStatus::Success);
    const Rect inherited{ 10.0f, 10.0f, 180.0f, 120.0f };
    u64 generation = 1u;
    for(const Rect& bounds : { Rect{ 20.0f, 20.0f, 0.0f, 40.0f }, Rect{ 20.0f, 20.0f, 40.0f, 0.0f }, Rect{ 20.0f, 20.0f, 0.0f, 0.0f }, Rect{ 220.0f, 20.0f, 40.0f, 40.0f } }){
        m_paint.begin({ 300.0f, 200.0f, 1.0f, 1.0f }, generation++, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
        m_paint.pushClip(inherited);
        ASSERT_TRUE(label.paint(m_text, m_paint, bounds));
        ExpectRect(m_paint.currentClip(), inherited);
        ASSERT_TRUE(m_paint.popClip());
        EXPECT_FALSE(m_paint.popClip());
        const DrawSnapshot snapshot = m_paint.freeze();
        EXPECT_TRUE(snapshot.commands().empty());
        EXPECT_TRUE(snapshot.vertices().empty());
        EXPECT_TRUE(snapshot.indices().empty());
    }
    ASSERT_EQ(label.setText({ .text = "MMMMMMMMMMMM" }), TextLayoutStatus::Success);
    m_paint.begin({ 300.0f, 200.0f, 1.0f, 1.0f }, generation, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
    m_paint.pushClip(inherited);
    EXPECT_FALSE(label.paint(m_text, m_paint, { 20.0f, 20.0f, 30.0f, 20.0f }, { 1.0f, 1.0f, 1.0f, 2.0f }));
    ExpectRect(m_paint.currentClip(), inherited);
    const Rect sibling{ 110.0f, 70.0f, 60.0f, 22.0f };
    ASSERT_TRUE(label.paint(m_text, m_paint, sibling));
    ExpectRect(m_paint.currentClip(), inherited);
    ASSERT_TRUE(m_paint.popClip());
    EXPECT_FALSE(m_paint.popClip());
    const DrawSnapshot snapshot = m_paint.freeze();
    ExpectGlyphRegion(snapshot, sibling);
}

TEST_F(UiTextRegionTests, OversizedButtonTextKeepsAsymmetricPaddingWithoutShrinkingItsSkinOrHitTarget){
    u64 generation = 1u;
    for(const f32 scale : { 1.0f, 1.5f, 2.0f }){
        ASSERT_TRUE(begin(generation, { 800.0f, 600.0f, scale, scale }));
        ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 20.0f, 220.0f, 130.0f }));
        WidgetOptions options;
        options.width = { LayoutSizePolicy::Fixed, 80.0f };
        options.height = { LayoutSizePolicy::Fixed, 28.0f };
        EXPECT_FALSE(m_builder.button("button", s_OverflowText, options));
        ASSERT_TRUE(finishPanel());
        EXPECT_FALSE(m_paint.popClip());
        const DrawSnapshot snapshot = m_paint.freeze();
        ASSERT_TRUE(m_context.commitFrame(generation++));
        const HitTarget* button = target(id("button", "panel"));
        ASSERT_NE(button, nullptr);
        ExpectRect(button->rectangle, { 13.0f, 24.0f, 80.0f, 28.0f });
        ExpectRect(button->clip, button->rectangle);
        const auto skin = sampleSkinPaint(snapshot, Name("button.normal"));
        ASSERT_TRUE(skin);
        ExpectRect(skin->bounds, button->rectangle);
        ExpectGlyphRegion(snapshot, { 15.0f, 27.0f, 74.0f, 20.0f });
    }
}

TEST_F(UiTextRegionTests, EmptyControlTextRegionsDoNotLeakIntoPaddingOrClipTheFollowingSibling){
    m_builder.listStyle().row.padding = { 7.0f, 11.0f, 13.0f, 3.0f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 20.0f, 220.0f, 240.0f }));
    WidgetOptions options;
    options.width = { LayoutSizePolicy::Fixed, 4.0f };
    options.height = { LayoutSizePolicy::Fixed, 40.0f };
    EXPECT_FALSE(m_builder.button("button", "MMMM", options));
    options.width.value = 10.0f;
    EXPECT_FALSE(m_builder.selectable("selectable", "MMMM", false, options));
    options.width.value = 20.0f;
    bool checked = false;
    EXPECT_FALSE(m_builder.checkbox("checkbox", "MMMM", checked, options));
    options.width.value = 50.0f;
    options.height.value = 32.0f;
    EXPECT_FALSE(m_builder.button("sibling", "MMMMMMMM", options));
    ASSERT_TRUE(finishPanel());
    EXPECT_FALSE(m_paint.popClip());
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(1u));
    const HitTarget* sibling = target(id("sibling", "panel"));
    ASSERT_NE(sibling, nullptr);
    ExpectGlyphRegion(snapshot, { sibling->rectangle.x + 2.0f, sibling->rectangle.y + 3.0f, 44.0f, 24.0f });
}

TEST_F(UiTextRegionTests, CheckboxGlyphOverhangCannotEnterIndicatorReservationOrEscapeAncestorClip){
    m_builder.style().fontSize = 48.0f;
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 20.0f, 100.0f, 110.0f }));
    WidgetOptions options;
    options.width = { LayoutSizePolicy::Fixed, 120.0f };
    options.height = { LayoutSizePolicy::Fixed, 36.0f };
    bool checked = false;
    EXPECT_FALSE(m_builder.checkbox("checkbox", "jjjjjjjjjjjjjjjj", checked, options));
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(1u));
    const HitTarget* checkbox = target(id("checkbox", "panel"));
    ASSERT_NE(checkbox, nullptr);
    ExpectRect(checkbox->rectangle, { 13.0f, 24.0f, 120.0f, 36.0f });
    ExpectRect(checkbox->clip, { 13.0f, 24.0f, 92.0f, 36.0f });
    ExpectGlyphRegion(snapshot, { 45.0f, 24.0f, 60.0f, 36.0f });
    const auto skin = sampleSkinPaint(snapshot, Name("checkbox.normal"));
    ASSERT_TRUE(skin);
    ExpectRect(skin->bounds, { 13.0f, 30.0f, 24.0f, 24.0f });
}

TEST_F(UiTextRegionTests, OversizedSelectableTextKeepsAllFourPaddingEdgesAndFullRowHitBounds){
    m_builder.listStyle().row.padding = { 7.0f, 11.0f, 13.0f, 3.0f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 20.0f, 220.0f, 130.0f }));
    WidgetOptions options;
    options.width = { LayoutSizePolicy::Fixed, 90.0f };
    options.height = { LayoutSizePolicy::Fixed, 34.0f };
    EXPECT_FALSE(m_builder.selectable("selectable", s_OverflowText, false, options));
    ASSERT_TRUE(finishPanel());
    EXPECT_FALSE(m_paint.popClip());
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_TRUE(m_context.commitFrame(1u));
    const HitTarget* selectable = target(id("selectable", "panel"));
    ASSERT_NE(selectable, nullptr);
    ExpectRect(selectable->rectangle, { 13.0f, 24.0f, 90.0f, 34.0f });
    ExpectRect(selectable->clip, selectable->rectangle);
    const auto skin = sampleSkinPaint(snapshot, Name("button.normal"));
    ASSERT_TRUE(skin);
    ExpectRect(skin->bounds, selectable->rectangle);
    ExpectGlyphRegion(snapshot, { 20.0f, 35.0f, 70.0f, 20.0f });
}

TEST_F(UiTextRegionTests, SelectableCentersWithinAsymmetricVerticalPaddingInsteadOfTheWholeRow){
    const StringView text = "MMMMMMMMMMMMMMMM";
    m_builder.listStyle().row.padding = { 7.0f, 11.0f, 13.0f, 3.0f };
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 20.0f, 220.0f, 130.0f }));
    WidgetOptions options;
    options.width = { LayoutSizePolicy::Fixed, 90.0f };
    options.height = { LayoutSizePolicy::Fixed, 70.0f };
    EXPECT_FALSE(m_builder.selectable("selectable", text, false, options));
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot actual = m_paint.freeze();
    const Rect content{ 20.0f, 35.0f, 70.0f, 56.0f };
    ExpectGlyphRegion(actual, content);
    auto layout = m_text.layout({ .text = text, .fontSize = m_builder.style().fontSize });
    ASSERT_TRUE(layout);
    ASSERT_LT(layout->measure().y, content.height);
    PaintBuilder reference(m_arena);
    reference.begin({ 800.0f, 600.0f, 1.0f, 1.0f }, 2u, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
    reference.pushClip(content);
    ASSERT_TRUE(m_text.paint(reference, *layout, { content.x, content.y + (content.height - layout->measure().y) * 0.5f }, m_builder.style().text));
    ASSERT_TRUE(reference.popClip());
    const DrawSnapshot expected = reference.freeze();
    Core::Alloc::ScratchArena scratch(Name("tests/ui/text_regions/centering"));
    Vector<Vertex, Core::Alloc::ScratchArena> actualGlyphs(scratch);
    Vector<Vertex, Core::Alloc::ScratchArena> expectedGlyphs(scratch);
    CopyGlyphVertices(actual, actualGlyphs);
    CopyGlyphVertices(expected, expectedGlyphs);
    ASSERT_EQ(actualGlyphs.size(), expectedGlyphs.size());
    ASSERT_GT(actualGlyphs.size(), 0u);
    for(usize index = 0u; index < actualGlyphs.size(); ++index){
        EXPECT_FLOAT_EQ(actualGlyphs[index].position.x, expectedGlyphs[index].position.x);
        EXPECT_FLOAT_EQ(actualGlyphs[index].position.y, expectedGlyphs[index].position.y);
        EXPECT_FLOAT_EQ(actualGlyphs[index].texCoord.x, expectedGlyphs[index].texCoord.x);
        EXPECT_FLOAT_EQ(actualGlyphs[index].texCoord.y, expectedGlyphs[index].texCoord.y);
    }
}

TEST_F(UiTextRegionTests, WindowTitleInkKeepsPaddingAndCollapseReservationWhenViewportShrinks){
    m_builder.style().fontSize = 24.0f;
    u64 generation = 1u;
    for(const bool collapsible : { false, true }){
        WindowState state;
        WindowOptions options;
        options.initialBounds = { 10.0f, 15.0f, 75.0f, 100.0f };
        options.minimumSize = { 0.0f, 0.0f };
        options.collapsible = collapsible;
        options.resizable = false;
        for(const f32 width : { 800.0f, 100.0f }){
            const f32 scale = width == 800.0f ? 1.0f : 2.0f;
            ASSERT_TRUE(begin(generation, { width, 200.0f, scale, scale }));
            ASSERT_TRUE(m_builder.beginWindow("window", "jjjjjjjjjjjjjjjjjjjjjjjjjjjjjjjj", state, options));
            ASSERT_TRUE(finishWindow());
            EXPECT_FALSE(m_paint.popClip());
            const DrawSnapshot snapshot = m_paint.freeze();
            ASSERT_TRUE(m_context.commitFrame(generation++));
            const HitTarget* title = target(id("@window.title"));
            ASSERT_NE(title, nullptr);
            const Rect& header = title->rectangle;
            const f32 left = header.x + 5.0f + (collapsible ? 28.0f : 0.0f);
            const f32 right = Min(width, header.x + header.width - 7.0f);
            ExpectGlyphRegion(snapshot, { left, header.y + 6.0f, right - left, header.height - 14.0f });
            const auto skin = sampleSkinPaint(snapshot, Name("window.title"));
            ASSERT_TRUE(skin);
            ExpectRect(skin->bounds, title->clip);
            if(collapsible){
                const HitTarget* collapse = target(id("@window.collapse"));
                ASSERT_NE(collapse, nullptr);
                EXPECT_FLOAT_EQ(collapse->rectangle.x, header.x + 5.0f);
                EXPECT_FLOAT_EQ(collapse->rectangle.width, 20.0f);
                EXPECT_LE(collapse->rectangle.x + collapse->rectangle.width, left);
            }
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

