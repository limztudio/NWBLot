// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"
#include "combo_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/label.h>
#include <impl/ecs_ui/toolkit/widgets/selectable_paint.h>

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

// Compare nonzero native raster coverage rather than transparent atlas borders that may legitimately be clipped.
[[nodiscard]] static Expected<f64> GlyphCoverageArea(const DrawSnapshot& snapshot)noexcept{
    f64 area = 0.0;
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material == PaintMaterial::SdfGlyph)
            return MakeUnexpected(Failure{});
        if(command.material != PaintMaterial::Glyph)
            continue;
        if(command.glyphPageIndex >= snapshot.glyphPages().size() || command.indexCount % 6u != 0u || static_cast<usize>(command.firstIndex) + command.indexCount > snapshot.indices().size())
            return MakeUnexpected(Failure{});
        const GlyphPage& page = *snapshot.glyphPages()[command.glyphPageIndex];
        const GlyphPageBinding& binding = page.binding();
        if(page.pixels().size() != static_cast<usize>(binding.width) * binding.height)
            return MakeUnexpected(Failure{});
        for(u32 index = command.firstIndex; index < command.firstIndex + command.indexCount; index += 6u){
            if(snapshot.indices()[index] >= snapshot.vertices().size() || snapshot.indices()[index + 2u] >= snapshot.vertices().size())
                return MakeUnexpected(Failure{});
            const Vertex& first = snapshot.vertices()[snapshot.indices()[index]];
            const Vertex& last = snapshot.vertices()[snapshot.indices()[index + 2u]];
            const f64 left = static_cast<f64>(first.texCoord.x) * binding.width;
            const f64 top = static_cast<f64>(first.texCoord.y) * binding.height;
            const f64 right = static_cast<f64>(last.texCoord.x) * binding.width;
            const f64 bottom = static_cast<f64>(last.texCoord.y) * binding.height;
            if(!IsFinite(left) || !IsFinite(top) || !IsFinite(right) || !IsFinite(bottom) || left < 0.0 || top < 0.0 || right > binding.width || bottom > binding.height || right <= left || bottom <= top)
                return MakeUnexpected(Failure{});
            const f64 width = static_cast<f64>(last.position.x) - first.position.x;
            const f64 height = static_cast<f64>(last.position.y) - first.position.y;
            if(!IsFinite(width) || !IsFinite(height) || width <= 0.0 || height <= 0.0)
                return MakeUnexpected(Failure{});
            const f64 texelArea = (width / (right - left)) * (height / (bottom - top));
            const u32 endX = static_cast<u32>(Ceil(right));
            const u32 endY = static_cast<u32>(Ceil(bottom));
            for(u32 y = static_cast<u32>(Floor(top)); y < endY; ++y){
                const f64 visibleHeight = Min(bottom, static_cast<f64>(y) + 1.0) - Max(top, static_cast<f64>(y));
                for(u32 x = static_cast<u32>(Floor(left)); x < endX; ++x){
                    const u8 coverage = page.pixels()[static_cast<usize>(y) * binding.width + x];
                    if(coverage == 0u)
                        continue;
                    const f64 visibleWidth = Min(right, static_cast<f64>(x) + 1.0) - Max(left, static_cast<f64>(x));
                    area += (static_cast<f64>(coverage) / 255.0) * visibleWidth * visibleHeight * texelArea;
                }
            }
        }
    }
    return area;
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


class IntrinsicRowSource final : public UiComboTests::ComboSource{
public:
    [[nodiscard]] virtual StringView text(const u64 index)const override{
        ++textCalls;
        return index == specialIndex ? specialText : ordinaryText;
    }


public:
    StringView ordinaryText = "Next";
    StringView specialText = "A\xCC\x81\xCC\x81 g\xCC\xA3";
    u64 specialIndex = 1u;
};


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

    void useDefaultButtonPadding(){
        UiSkin::RegionVector regions(m_arena);
        regions.assign(m_skin.regions().begin(), m_skin.regions().end());
        for(UiSkinRegion& region : regions){
            if(region.name == Name("button.normal") || region.name == Name("button.hover") || region.name == Name("button.pressed") || region.name == Name("button.disabled")){
                region.padding = { 8.0f, 8.0f, 8.0f, 8.0f };
                region.minimumHeight = 32.0f;
            }
        }
        m_skin.setAtlas(m_skin.texture(), m_skin.atlasWidth(), m_skin.atlasHeight(), m_skin.referenceDensity(), Move(regions));
        ASSERT_TRUE(m_skin.validatePayload());
        m_builder.setSkin(m_skin);
    }

    void checkIntrinsicRowCoverage(IntrinsicRowSource& source, ListState& list, ComboState& combo, const bool popup, const f32 fontSize, const DisplayMetrics& display, const u64 generation){
        SCOPED_TRACE(testing::Message() << "popup=" << popup << ", fontSize=" << fontSize << ", scale=" << display.pixelScaleX << "/" << display.pixelScaleY);
        m_builder.style().fontSize = fontSize;
        const auto rowMetrics = SelectablePainter::Measure(m_skin, m_builder.listStyle().row);
        ASSERT_TRUE(rowMetrics);
        ASSERT_TRUE(begin(generation, display));
        ASSERT_TRUE(m_builder.beginPanel("rows_panel", { 10.0f, 20.0f, 600.0f, 540.0f }));
        const WidgetId owner = id(popup ? "combo" : "list", "rows_panel");
        if(popup){
            ASSERT_TRUE(ComboBehavior::Reconcile(combo, source));
            if(!combo.isOpen())
                combo.open();
            ComboOptions options;
            options.width = { LayoutSizePolicy::Fixed, 400.0f };
            options.popupHeight = 320.0f;
            options.placeholder = "";
            ASSERT_TRUE(m_builder.comboBox("combo", source, combo, options).valid);
        }
        else{
            ListOptions options;
            options.width = { LayoutSizePolicy::Fixed, 400.0f };
            options.height = { LayoutSizePolicy::Fixed, 320.0f };
            ASSERT_TRUE(m_builder.virtualList("list", source, list, options).valid);
        }
        ASSERT_TRUE(finishPanel());
        const DrawSnapshot actual = m_paint.freeze();
        ASSERT_TRUE(m_context.commitFrame(generation));
        const ListState& state = popup ? combo.listState() : list;
        const ScrollPlacement& placement = state.placement();
        ASSERT_GT(placement.endRow, placement.firstRow);
        const WidgetId listOwner = popup ? MakeWidgetId(owner, "rows") : owner;
        const WidgetId rows = MakeWidgetId(listOwner, "rows");
        const HitTarget* first = target(MakeWidgetPartId(rows, placement.firstRow + 1u));
        ASSERT_NE(first, nullptr);
        const f32 rowHeight = first->rectangle.height;
        f32 expectedHeight = rowMetrics->minimum.y;
        for(const StringView label : { StringView{}, source.ordinaryText, source.specialText }){
            const auto layout = m_text.layout({ .text = label, .fontSize = fontSize });
            ASSERT_TRUE(layout);
            const auto visual = m_text.measure(*layout, display);
            ASSERT_TRUE(visual);
            expectedHeight = Max(expectedHeight, visual->height + rowMetrics->padding.top + rowMetrics->padding.bottom);
        }
        EXPECT_FLOAT_EQ(rowHeight, expectedHeight);
        EXPECT_DOUBLE_EQ(placement.contentHeight, static_cast<f64>(rowHeight) * source.count);
        const HitTarget* host = target(listOwner);
        ASSERT_NE(host, nullptr);
        EXPECT_DOUBLE_EQ(host->scrollStep, static_cast<f64>(rowHeight) * 3.0);
        PaintBuilder reference(m_arena);
        reference.begin(display, generation, m_skinGeneration, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
        reference.pushClip(placement.contentClip);
        for(u64 index = placement.firstRow; index < placement.endRow; ++index){
            const HitTarget* row = target(MakeWidgetPartId(rows, index + 1u));
            ASSERT_NE(row, nullptr);
            EXPECT_FLOAT_EQ(row->rectangle.height, rowHeight);
            const StringView label = index == source.specialIndex ? source.specialText : source.ordinaryText;
            const auto layout = m_text.layout({ .text = label, .fontSize = fontSize });
            ASSERT_TRUE(layout);
            const auto visual = m_text.measure(*layout, display);
            ASSERT_TRUE(visual);
            const Insets& padding = rowMetrics->padding;
            EXPECT_GE(rowHeight + 0.0001f, visual->height + padding.top + padding.bottom);
            const f32 contentHeight = rowHeight - padding.top - padding.bottom;
            const Point origin{ row->rectangle.x + padding.left - visual->x, row->rectangle.y + padding.top + Max(0.0f, (contentHeight - visual->height) * 0.5f) - visual->y };
            ASSERT_TRUE(m_text.paint(reference, *layout, origin, m_builder.style().text));
        }
        ASSERT_TRUE(reference.popClip());
        const auto expectedCoverage = GlyphCoverageArea(reference.freeze());
        const auto actualCoverage = GlyphCoverageArea(actual);
        ASSERT_TRUE(expectedCoverage);
        ASSERT_TRUE(actualCoverage);
        ASSERT_GT(*expectedCoverage, 0.0);
        EXPECT_NEAR(*actualCoverage, *expectedCoverage, Max(0.00001, *expectedCoverage * 0.000002));
    }

    void checkContentButtonRow(const StringView firstText, const StringView secondText, const f32 fontSize, const f32 scale, const u64 generation, const bool fixedHeight = false){
        SCOPED_TRACE(testing::Message() << "fontSize=" << fontSize << ", scale=" << scale << ", fixedHeight=" << fixedHeight);
        m_builder.style().fontSize = fontSize;
        const DisplayMetrics display{ 800.0f, 600.0f, scale, scale };
        auto firstLayout = m_text.layout({ .text = firstText, .fontSize = fontSize });
        auto secondLayout = m_text.layout({ .text = secondText, .fontSize = fontSize });
        ASSERT_TRUE(firstLayout);
        ASSERT_TRUE(secondLayout);
        const Point firstLogical = firstLayout->measure();
        const Point secondLogical = secondLayout->measure();
        const auto firstMeasured = m_text.measure(*firstLayout, display);
        const auto secondMeasured = m_text.measure(*secondLayout, display);
        ASSERT_TRUE(firstMeasured);
        ASSERT_TRUE(secondMeasured);
        EXPECT_FLOAT_EQ(firstLayout->measure().y, firstLogical.y);
        EXPECT_FLOAT_EQ(secondLayout->measure().y, secondLogical.y);
        EXPECT_GE(firstMeasured->height, firstLogical.y);
        EXPECT_GE(secondMeasured->height, secondLogical.y);
        const f32 firstHeight = fixedHeight ? 32.0f : Max(32.0f, (firstMeasured->height + 8.0f) + 8.0f);
        const f32 secondHeight = fixedHeight ? 32.0f : Max(32.0f, (secondMeasured->height + 8.0f) + 8.0f);
        ASSERT_TRUE(begin(generation, display));
        ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 20.0f, 740.0f, 480.0f }));
        ContainerOptions row;
        if(fixedHeight)
            row.height = { LayoutSizePolicy::Fixed, 32.0f };
        ASSERT_TRUE(m_builder.beginRow("navigation", row));
        WidgetOptions navigation;
        navigation.width = { LayoutSizePolicy::Fixed, Max(100.0f, Max(firstMeasured->width, secondMeasured->width) + 16.0f) };
        if(fixedHeight)
            navigation.height = { LayoutSizePolicy::Fixed, 32.0f };
        EXPECT_FALSE(m_builder.button("previous", firstText, navigation));
        EXPECT_FALSE(m_builder.button("next", secondText, navigation));
        ASSERT_TRUE(m_builder.endContainer());
        WidgetOptions marker;
        marker.width = { LayoutSizePolicy::Fixed, 40.0f };
        marker.height = { LayoutSizePolicy::Fixed, 24.0f };
        EXPECT_FALSE(m_builder.button("after", "", marker));
        ASSERT_TRUE(finishPanel());
        const DrawSnapshot actual = m_paint.freeze();
        ASSERT_TRUE(m_context.commitFrame(generation));
        const WidgetId rowId = id("navigation", "panel");
        const HitTarget* first = target(MakeWidgetId(rowId, "previous"));
        const HitTarget* second = target(MakeWidgetId(rowId, "next"));
        const HitTarget* after = target(id("after", "panel"));
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);
        ASSERT_NE(after, nullptr);
        EXPECT_FLOAT_EQ(first->rectangle.height, firstHeight);
        EXPECT_FLOAT_EQ(second->rectangle.height, secondHeight);
        EXPECT_FLOAT_EQ(second->rectangle.y, first->rectangle.y);
        EXPECT_FLOAT_EQ(after->rectangle.y, first->rectangle.y + Max(firstHeight, secondHeight) + m_builder.style().gap);
        ExpectRect(first->clip, first->rectangle);
        ExpectRect(second->clip, second->rectangle);
        PaintBuilder reference(m_arena);
        reference.begin(display, generation, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
        const Rect controls[]{ first->rectangle, second->rectangle };
        const Rect measured[]{ *firstMeasured, *secondMeasured };
        const TextLayout* layouts[]{ &*firstLayout, &*secondLayout };
        for(u32 index = 0u; index < 2u; ++index){
            const Rect& bounds = controls[index];
            const Rect& visual = measured[index];
            const f32 x = bounds.x + 8.0f + Max(0.0f, (bounds.width - 16.0f - visual.width) * 0.5f) - visual.x;
            const f32 y = bounds.y + 8.0f + Max(0.0f, (bounds.height - 16.0f - visual.height) * 0.5f) - visual.y;
            ASSERT_TRUE(m_text.paint(reference, *layouts[index], { x, y }, m_builder.style().text));
        }
        const DrawSnapshot expected = reference.freeze();
        const auto actualCoverage = GlyphCoverageArea(actual);
        const auto expectedCoverage = GlyphCoverageArea(expected);
        ASSERT_TRUE(actualCoverage);
        ASSERT_TRUE(expectedCoverage);
        ASSERT_GT(*expectedCoverage, 0.0);
        if(fixedHeight)
            EXPECT_LT(*actualCoverage, *expectedCoverage * 0.999);
        else
            EXPECT_NEAR(*actualCoverage, *expectedCoverage, Max(0.00001, *expectedCoverage * 0.000002));
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
    ExpectGlyphRegion(snapshot, { 20.0f, 35.0f, 70.0f, 18.0f });
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
    const Rect content{ 20.0f, 35.0f, 70.0f, 54.0f };
    ExpectGlyphRegion(actual, content);
    auto layout = m_text.layout({ .text = text, .fontSize = m_builder.style().fontSize });
    ASSERT_TRUE(layout);
    const DisplayMetrics display{ 800.0f, 600.0f, 1.0f, 1.0f };
    const auto measured = m_text.measure(*layout, display);
    ASSERT_TRUE(measured);
    ASSERT_LT(measured->height, content.height);
    PaintBuilder reference(m_arena);
    reference.begin(display, 2u, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
    reference.pushClip(content);
    const Point origin{ content.x - measured->x, content.y + (content.height - measured->height) * 0.5f - measured->y };
    ASSERT_TRUE(m_text.paint(reference, *layout, origin, m_builder.style().text));
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

TEST_F(UiTextRegionTests, ContentNavigationHeightPreservesCoverageThatFixed32PixelRowsCut){
    ASSERT_NO_FATAL_FAILURE(useDefaultButtonPadding());
    ASSERT_NO_FATAL_FAILURE(checkContentButtonRow("Previous", "Next", 16.0f, 1.0f, 1u, true));
    u64 generation = 2u;
    for(const f32 fontSize : { 12.0f, 16.0f, 22.0f }){
        for(const f32 scale : { 1.0f, 1.25f, 1.5f, 2.0f })
            ASSERT_NO_FATAL_FAILURE(checkContentButtonRow("Previous", "Next", fontSize, scale, generation++));
    }
}

TEST_F(UiTextRegionTests, ContentRowsPreserveDescendersStackedAccentsMultilineAndKoreanFallbackCoverage){
    ASSERT_NO_FATAL_FAILURE(useDefaultButtonPadding());
    ASSERT_TRUE(installKoreanFallback());
    const StringView samples[]{
        "Agjpqy A\xCC\x81 e\xCC\x88 ",
        "A\xCC\x81\xCC\x81 g\xCC\xA3 ",
        "Previous\nA\xCC\x81\xCC\x81 gjpqy g\xCC\xA3 ",
        "\xED\x95\x9C\xEA\xB8\x80 Agjpqy A\xCC\x81 e\xCC\x88 ",
    };
    auto mixed = m_text.layout({ .text = samples[3u] });
    ASSERT_TRUE(mixed);
    bool sawLatin = false;
    bool sawKorean = false;
    for(const PlacedGlyph& glyph : mixed->glyphs()){
        sawLatin = sawLatin || glyph.face->identity().name() == Name("tests/ui/fonts/latin");
        sawKorean = sawKorean || glyph.face->identity().name() == Name("tests/ui/fonts/korean");
    }
    ASSERT_TRUE(sawLatin);
    ASSERT_TRUE(sawKorean);
    u64 generation = 1u;
    for(u32 index = 0u; index < LengthOf(samples); ++index){
        SCOPED_TRACE(testing::Message() << "sample=" << index);
        for(const f32 fontSize : { 12.0f, 16.0f, 22.0f }){
            for(const f32 scale : { 1.0f, 1.25f, 1.5f, 2.0f })
                ASSERT_NO_FATAL_FAILURE(checkContentButtonRow("Next", samples[index], fontSize, scale, generation++));
        }
    }
}

TEST_F(UiTextRegionTests, NaturalEditHeightKeepsCombiningAndFallbackInkOutsideLogicalLineMetrics){
    ASSERT_NO_FATAL_FAILURE(useDefaultButtonPadding());
    ASSERT_TRUE(installKoreanFallback());
    m_builder.style().fontSize = 16.0f;
    m_builder.editStyle().padding = { 8.0f, 8.0f, 8.0f, 8.0f };
    const StringView samples[]{ "A\xCC\x81\xCC\x81 g\xCC\xA3 ", "A\xCC\x81\xCC\x81 g\xCC\xA3 \xED\x95\x9C\xEA\xB8\x80 " };
    u64 generation = 1u;
    for(const StringView sample : samples){
        for(const f32 scale : { 1.0f, 1.25f, 1.5f, 2.0f }){
            SCOPED_TRACE(testing::Message() << "scale=" << scale);
            const DisplayMetrics display{ 800.0f, 600.0f, scale, scale };
            EditModel model(m_arena);
            ASSERT_TRUE(model.setText(sample));
            EditBoxState state;
            auto layout = m_text.layout({ .text = sample, .fontSize = 16.0f });
            ASSERT_TRUE(layout);
            const auto measured = m_text.measure(*layout, display);
            ASSERT_TRUE(measured);
            ASSERT_TRUE(begin(generation, display));
            ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 20.0f, 740.0f, 200.0f }));
            EditBoxOptions options;
            options.width = { LayoutSizePolicy::Fixed, 300.0f };
            ASSERT_TRUE(m_builder.editBox("edit", model, state, options).valid);
            ASSERT_TRUE(finishPanel());
            const DrawSnapshot actual = m_paint.freeze();
            ASSERT_TRUE(m_context.commitFrame(generation));
            EXPECT_FLOAT_EQ(state.placement.bounds.height, Max(32.0f, (measured->height + 8.0f) + 8.0f));
            EXPECT_FLOAT_EQ(state.placement.scroll, 0.0f);
            EXPECT_GE(state.placement.textOrigin.y + measured->y, state.placement.content.y - 0.0001f);
            EXPECT_LE(state.placement.textOrigin.y + measured->y + measured->height, state.placement.content.y + state.placement.content.height + 0.0001f);
            PaintBuilder reference(m_arena);
            reference.begin(display, generation++, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
            ASSERT_TRUE(m_text.paint(reference, *layout, state.placement.textOrigin, m_builder.editStyle().text));
            const auto expectedCoverage = GlyphCoverageArea(reference.freeze());
            const auto actualCoverage = GlyphCoverageArea(actual);
            ASSERT_TRUE(expectedCoverage);
            ASSERT_TRUE(actualCoverage);
            ASSERT_GT(*expectedCoverage, 0.0);
            EXPECT_NEAR(*actualCoverage, *expectedCoverage, Max(0.00001, *expectedCoverage * 0.000002));
        }
    }
}

TEST_F(UiTextRegionTests, IntrinsicListAndComboRowsPreserveActualMarksFallbackAndMultilineCoverage){
    ASSERT_TRUE(installKoreanFallback());
    IntrinsicRowSource source;
    source.count = 3u;
    source.ordinaryText = "A\xCC\x81\xCC\x81 g\xCC\xA3 gjpqy";
    source.specialText = "\xED\x95\x9C\xEA\xB8\x80 Next\nA\xCC\x81\xCC\x81 g\xCC\xA3";
    ListState list;
    ComboState combo;
    u64 generation = 1u;
    for(const bool popup : { false, true }){
        for(const f32 fontSize : { 12.0f, 16.0f, 22.0f }){
            for(const Point scale : { Point{ 1.0f, 1.0f }, Point{ 1.25f, 1.25f }, Point{ 1.5f, 1.5f }, Point{ 2.0f, 2.0f }, Point{ 1.25f, 1.5f } })
                ASSERT_NO_FATAL_FAILURE(checkIntrinsicRowCoverage(source, list, combo, popup, fontSize, { 800.0f, 600.0f, scale.x, scale.y }, generation++));
        }
    }
}

TEST_F(UiTextRegionTests, IntrinsicRowsShrinkAfterSourceFontSizeAndResolvedStyleChanges){
    IntrinsicRowSource source;
    source.count = 3u;
    source.specialText = "Previous\nA\xCC\x81\xCC\x81 g\xCC\xA3\nNext";
    ListState list;
    ComboState combo;
    const DisplayMetrics display{ 800.0f, 600.0f, 1.0f, 1.0f };
    u64 generation = 1u;
    for(const bool popup : { false, true }){
        source.specialText = "Previous\nA\xCC\x81\xCC\x81 g\xCC\xA3\nNext";
        ++source.contentRevision;
        m_builder.listStyle().row.padding = { 8.0f, 16.0f, 8.0f, 18.0f };
        ASSERT_NO_FATAL_FAILURE(checkIntrinsicRowCoverage(source, list, combo, popup, 22.0f, display, generation++));
        ASSERT_NO_FATAL_FAILURE(checkIntrinsicRowCoverage(source, list, combo, popup, 12.0f, display, generation++));
        m_builder.listStyle().row.padding = { 8.0f, 4.0f, 8.0f, 4.0f };
        ASSERT_NO_FATAL_FAILURE(checkIntrinsicRowCoverage(source, list, combo, popup, 12.0f, display, generation++));
        source.specialText = "Next";
        ++source.contentRevision;
        ASSERT_NO_FATAL_FAILURE(checkIntrinsicRowCoverage(source, list, combo, popup, 12.0f, display, generation++));
        source.specialText = "A\xCC\x81\xCC\x81 g\xCC\xA3";
        ++source.generation;
        ASSERT_NO_FATAL_FAILURE(checkIntrinsicRowCoverage(source, list, combo, popup, 16.0f, display, generation++));
    }
}

TEST_F(UiTextRegionTests, SelectableMetricsUseEveryStatePaddingAndMinimumWithoutHoverMovement){
    UiSkin::RegionVector regions(m_arena);
    regions.assign(m_skin.regions().begin(), m_skin.regions().end());
    for(UiSkinRegion& region : regions){
        if(region.name == Name("button.normal"))
            region.padding = { 9.0f, 1.0f, 2.0f, 3.0f };
        else if(region.name == Name("button.hover"))
            region.padding = { 1.0f, 11.0f, 2.0f, 3.0f };
        else if(region.name == Name("button.pressed")){
            region.padding = { 1.0f, 2.0f, 13.0f, 3.0f };
            region.minimumHeight = 55.0f;
        }
        else if(region.name == Name("button.disabled")){
            region.padding = { 1.0f, 2.0f, 3.0f, 15.0f };
            region.minimumWidth = 100.0f;
        }
    }
    m_skin.setAtlas(m_skin.texture(), m_skin.atlasWidth(), m_skin.atlasHeight(), m_skin.referenceDensity(), Move(regions));
    ASSERT_TRUE(m_skin.validatePayload());
    m_builder.setSkin(m_skin);
    const SelectableStyle style;
    const auto metrics = SelectablePainter::Measure(m_skin, style);
    ASSERT_TRUE(metrics);
    EXPECT_FLOAT_EQ(metrics->padding.left, 9.0f);
    EXPECT_FLOAT_EQ(metrics->padding.top, 11.0f);
    EXPECT_FLOAT_EQ(metrics->padding.right, 13.0f);
    EXPECT_FLOAT_EQ(metrics->padding.bottom, 15.0f);
    EXPECT_FLOAT_EQ(metrics->minimum.x, 100.0f);
    EXPECT_FLOAT_EQ(metrics->minimum.y, 55.0f);
    const auto layout = m_text.layout({ .text = "Agjpqy", .fontSize = 16.0f });
    ASSERT_TRUE(layout);
    const auto textBounds = m_text.measure(*layout, { 800.0f, 600.0f, 1.0f, 1.0f });
    ASSERT_TRUE(textBounds);
    Core::Alloc::ScratchArena scratch(Name("tests/ui/selectable_state_metrics"));
    Vector<Vertex, Core::Alloc::ScratchArena> baseline(scratch);
    u64 generation = 1u;
    for(const SelectablePaintFlags flags : { SelectablePaintFlags{}, SelectablePaintFlags{ true, false, true, false }, SelectablePaintFlags{ true, true, false, false }, SelectablePaintFlags{ false, false, false, false } }){
        m_paint.begin({ 800.0f, 600.0f, 1.0f, 1.0f }, generation++, m_skinGeneration, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
        ASSERT_TRUE(SelectablePainter::Paint(m_paint, m_text, m_skin, *layout, *textBounds, metrics->padding, { 20.0f, 30.0f, 180.0f, 70.0f }, { 0.0f, 0.0f, 800.0f, 600.0f }, style, flags, {}, {}));
        const DrawSnapshot snapshot = m_paint.freeze();
        ExpectGlyphRegion(snapshot, { 29.0f, 41.0f, 158.0f, 44.0f });
        Vector<Vertex, Core::Alloc::ScratchArena> glyphs(scratch);
        CopyGlyphVertices(snapshot, glyphs);
        if(baseline.empty())
            baseline = glyphs;
        ASSERT_EQ(glyphs.size(), baseline.size());
        for(usize index = 0u; index < glyphs.size(); ++index){
            EXPECT_FLOAT_EQ(glyphs[index].position.x, baseline[index].position.x);
            EXPECT_FLOAT_EQ(glyphs[index].position.y, baseline[index].position.y);
            EXPECT_FLOAT_EQ(glyphs[index].texCoord.x, baseline[index].texCoord.x);
            EXPECT_FLOAT_EQ(glyphs[index].texCoord.y, baseline[index].texCoord.y);
        }
    }
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

