// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"
#include "widget_id_assertion.h"
#include "combo_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/label.h>
#include <impl/ecs_ui/toolkit/widgets/selectable_paint.h>

#include <core/alloc/scratch.h>

#include <global/simdmath.h>


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
    const SIMDVectorSingle expectedEnd = VectorAdd(VectorSet(expected.x, expected.y, 0.0f, 0.0f), VectorSet(expected.width, expected.height, 0.0f, 0.0f));
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
            EXPECT_LE(position.x, VectorGetX(expectedEnd));
            EXPECT_GE(position.y, expected.y);
            EXPECT_LE(position.y, VectorGetY(expectedEnd));
        }
        count += command.indexCount;
    }
    EXPECT_GT(count, 0u);
}

// Compare nonzero native raster coverage rather than transparent atlas borders that may legitimately be clipped.
[[nodiscard]] static Expected<f64> GlyphCoverageArea(const DrawSnapshot& snapshot)noexcept{
    SIMDVectorDouble area = VectorDoubleZero();
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
        const SIMDVectorDouble textureSize = VectorDoubleSet(static_cast<f64>(binding.width), static_cast<f64>(binding.height), static_cast<f64>(binding.width), static_cast<f64>(binding.height));
        for(u32 index = command.firstIndex; index < command.firstIndex + command.indexCount; index += 6u){
            if(snapshot.indices()[index] >= snapshot.vertices().size() || snapshot.indices()[index + 2u] >= snapshot.vertices().size())
                return MakeUnexpected(Failure{});
            const Vertex& first = snapshot.vertices()[snapshot.indices()[index]];
            const Vertex& last = snapshot.vertices()[snapshot.indices()[index + 2u]];
            // Lanes are left, top, right, bottom in texels.
            const SIMDVectorDouble texel = VectorDoubleMultiply(VectorDoubleSet(first.texCoord.x, first.texCoord.y, last.texCoord.x, last.texCoord.y), textureSize);
            const f64 left = VectorDoubleGetX(texel);
            const f64 top = VectorDoubleGetY(texel);
            const f64 right = VectorDoubleGetZ(texel);
            const f64 bottom = VectorDoubleGetW(texel);
            if(!IsFinite(left) || !IsFinite(top) || !IsFinite(right) || !IsFinite(bottom) || left < 0.0 || top < 0.0 || right > binding.width || bottom > binding.height || right <= left || bottom <= top)
                return MakeUnexpected(Failure{});
            const SIMDVectorDouble size = VectorDoubleSubtract(VectorDoubleSet(last.position.x, last.position.y, 0.0, 0.0), VectorDoubleSet(first.position.x, first.position.y, 0.0, 0.0));
            const f64 width = VectorDoubleGetX(size);
            const f64 height = VectorDoubleGetY(size);
            if(!IsFinite(width) || !IsFinite(height) || width <= 0.0 || height <= 0.0)
                return MakeUnexpected(Failure{});
            const SIMDVectorDouble texelScale = VectorDoubleDivide(size, VectorDoubleSubtract(VectorDoubleSwizzle<2, 3, 0, 1>(texel), texel));
            const SIMDVectorDouble texelArea = VectorDoubleMultiply(VectorDoubleSplatX(texelScale), VectorDoubleSplatY(texelScale));
            const SIMDVectorDouble firstTexel = VectorDoubleFloor(texel);
            const SIMDVectorDouble endTexel = VectorDoubleCeiling(texel);
            const u32 endX = static_cast<u32>(VectorDoubleGetZ(endTexel));
            const u32 endY = static_cast<u32>(VectorDoubleGetW(endTexel));
            for(u32 y = static_cast<u32>(VectorDoubleGetY(firstTexel)); y < endY; ++y){
                const SIMDVectorDouble row = VectorDoubleReplicate(static_cast<f64>(y));
                const SIMDVectorDouble visibleHeight = VectorDoubleSubtract(VectorDoubleMin(VectorDoubleSplatW(texel), VectorDoubleAdd(row, VectorDoubleSplatOne())), VectorDoubleMax(VectorDoubleSplatY(texel), row));
                for(u32 x = static_cast<u32>(VectorDoubleGetX(firstTexel)); x < endX; ++x){
                    const u8 coverage = page.pixels()[static_cast<usize>(y) * binding.width + x];
                    if(coverage == 0u)
                        continue;
                    const SIMDVectorDouble column = VectorDoubleReplicate(static_cast<f64>(x));
                    const SIMDVectorDouble visibleWidth = VectorDoubleSubtract(VectorDoubleMin(VectorDoubleSplatZ(texel), VectorDoubleAdd(column, VectorDoubleSplatOne())), VectorDoubleMax(VectorDoubleSplatX(texel), column));
                    area = VectorDoubleMultiplyAddExpression(VectorDoubleMultiply(VectorDoubleMultiply(VectorDoubleDivide(VectorDoubleReplicate(static_cast<f64>(coverage)), VectorDoubleReplicate(255.0)), visibleWidth), visibleHeight), texelArea, area);
                }
            }
        }
    }
    return VectorDoubleGetX(area);
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
    [[nodiscard]] virtual StringView text(const u64 index)const noexcept override{
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
        for(u32 index = 0u; index < LengthOf(names); ++index){
            const auto regionName = Name::FromText(names[index]);
            ASSERT_TRUE(regionName);
            regions.push_back({ *regionName, { 88u + index * 8u, 0u, 8u, 8u }, {}, {}, 24.0f, 24.0f });
        }
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
        const WidgetId listOwner = popup ? UiTestSupport::RequireWidgetId(MakeWidgetId(owner, "rows")) : owner;
        const WidgetId rows = UiTestSupport::RequireWidgetId(MakeWidgetId(listOwner, "rows"));
        const HitTarget* first = target(UiTestSupport::RequireWidgetId(MakeWidgetPartId(rows, placement.firstRow + 1u)));
        ASSERT_NE(first, nullptr);
        const f32 rowHeight = first->rectangle.height;
        SIMDVectorSingle expectedHeight = VectorReplicate(rowMetrics->minimum.y);
        for(const StringView label : { StringView{}, source.ordinaryText, source.specialText }){
            const auto layout = m_text.layout({ .text = label, .fontSize = fontSize });
            ASSERT_TRUE(layout);
            const auto visual = m_text.measure(*layout, display);
            ASSERT_TRUE(visual);
            expectedHeight = VectorMax(expectedHeight, VectorAdd(VectorAdd(VectorReplicate(visual->height), VectorReplicate(rowMetrics->padding.top)), VectorReplicate(rowMetrics->padding.bottom)));
        }
        EXPECT_FLOAT_EQ(rowHeight, VectorGetX(expectedHeight));
        // Lanes are content height and scroll step.
        const SIMDVectorDouble listExtent = VectorDoubleMultiply(VectorDoubleReplicate(static_cast<f64>(rowHeight)), VectorDoubleSet(static_cast<f64>(source.count), 3.0, 0.0, 0.0));
        EXPECT_DOUBLE_EQ(placement.contentHeight, VectorDoubleGetX(listExtent));
        const HitTarget* host = target(listOwner);
        ASSERT_NE(host, nullptr);
        EXPECT_DOUBLE_EQ(host->scrollStep, VectorDoubleGetY(listExtent));
        PaintBuilder reference(m_arena);
        reference.begin(display, generation, m_skinGeneration, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
        reference.pushClip(placement.contentClip);
        for(u64 index = placement.firstRow; index < placement.endRow; ++index){
            const HitTarget* row = target(UiTestSupport::RequireWidgetId(MakeWidgetPartId(rows, index + 1u)));
            ASSERT_NE(row, nullptr);
            EXPECT_FLOAT_EQ(row->rectangle.height, rowHeight);
            const StringView label = index == source.specialIndex ? source.specialText : source.ordinaryText;
            const auto layout = m_text.layout({ .text = label, .fontSize = fontSize });
            ASSERT_TRUE(layout);
            const auto visual = m_text.measure(*layout, display);
            ASSERT_TRUE(visual);
            const Insets& padding = rowMetrics->padding;
            const SIMDVectorSingle paddedHeight = VectorAdd(VectorAdd(VectorReplicate(visual->height), VectorReplicate(padding.top)), VectorReplicate(padding.bottom));
            EXPECT_GE(VectorGetX(VectorAdd(VectorReplicate(rowHeight), VectorReplicate(0.0001f))), VectorGetX(paddedHeight));
            const SIMDVectorSingle contentHeight = VectorSubtract(VectorSubtract(VectorReplicate(rowHeight), VectorReplicate(padding.top)), VectorReplicate(padding.bottom));
            const SIMDVectorSingle centering = VectorMax(VectorZero(), VectorMultiply(VectorSubtract(contentHeight, VectorReplicate(visual->height)), VectorReplicate(0.5f)));
            const SIMDVectorSingle start = VectorAdd(VectorSet(row->rectangle.x, row->rectangle.y, 0.0f, 0.0f), VectorSet(padding.left, padding.top, 0.0f, 0.0f));
            // Only y gets the centering offset.
            const SIMDVectorSingle originValue = VectorSubtract(VectorSelect(start, VectorAdd(start, centering), s_SIMDMaskY), VectorSet(visual->x, visual->y, 0.0f, 0.0f));
            const Point origin{ VectorGetX(originValue), VectorGetY(originValue) };
            ASSERT_TRUE(m_text.paint(reference, *layout, origin, m_builder.style().text));
        }
        ASSERT_TRUE(reference.popClip());
        const auto expectedCoverage = GlyphCoverageArea(reference.freeze());
        const auto actualCoverage = GlyphCoverageArea(actual);
        ASSERT_TRUE(expectedCoverage);
        ASSERT_TRUE(actualCoverage);
        ASSERT_GT(*expectedCoverage, 0.0);
        EXPECT_NEAR(*actualCoverage, *expectedCoverage, VectorDoubleGetX(VectorDoubleMax(VectorDoubleReplicate(0.00001), VectorDoubleMultiply(VectorDoubleReplicate(*expectedCoverage), VectorDoubleReplicate(0.000002)))));
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
        // Lanes are the first and second button heights.
        const SIMDVectorSingle heights = fixedHeight ? VectorReplicate(32.0f) : VectorMax(VectorReplicate(32.0f), VectorAdd(VectorAdd(VectorSet(firstMeasured->height, secondMeasured->height, 0.0f, 0.0f), VectorReplicate(8.0f)), VectorReplicate(8.0f)));
        ASSERT_TRUE(begin(generation, display));
        ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 20.0f, 740.0f, 480.0f }));
        ContainerOptions row;
        if(fixedHeight)
            row.height = { LayoutSizePolicy::Fixed, 32.0f };
        ASSERT_TRUE(m_builder.beginRow("navigation", row));
        WidgetOptions navigation;
        navigation.width = { LayoutSizePolicy::Fixed, VectorGetX(VectorMax(VectorReplicate(100.0f), VectorAdd(VectorMax(VectorReplicate(firstMeasured->width), VectorReplicate(secondMeasured->width)), VectorReplicate(16.0f)))) };
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
        const HitTarget* first = target(UiTestSupport::RequireWidgetId(MakeWidgetId(rowId, "previous")));
        const HitTarget* second = target(UiTestSupport::RequireWidgetId(MakeWidgetId(rowId, "next")));
        const HitTarget* after = target(id("after", "panel"));
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);
        ASSERT_NE(after, nullptr);
        EXPECT_FLOAT_EQ(first->rectangle.height, VectorGetX(heights));
        EXPECT_FLOAT_EQ(second->rectangle.height, VectorGetY(heights));
        EXPECT_FLOAT_EQ(second->rectangle.y, first->rectangle.y);
        EXPECT_FLOAT_EQ(after->rectangle.y, VectorGetX(VectorAdd(VectorAdd(VectorReplicate(first->rectangle.y), VectorMax(VectorSplatX(heights), VectorSplatY(heights))), VectorReplicate(m_builder.style().gap))));
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
            const SIMDVectorSingle slack = VectorSubtract(VectorSubtract(VectorSet(bounds.width, bounds.height, 0.0f, 0.0f), VectorReplicate(16.0f)), VectorSet(visual.width, visual.height, 0.0f, 0.0f));
            const SIMDVectorSingle centered = VectorAdd(VectorAdd(VectorSet(bounds.x, bounds.y, 0.0f, 0.0f), VectorReplicate(8.0f)), VectorMax(VectorZero(), VectorMultiply(slack, VectorReplicate(0.5f))));
            const SIMDVectorSingle origin = VectorSubtract(centered, VectorSet(visual.x, visual.y, 0.0f, 0.0f));
            ASSERT_TRUE(m_text.paint(reference, *layouts[index], { VectorGetX(origin), VectorGetY(origin) }, m_builder.style().text));
        }
        const DrawSnapshot expected = reference.freeze();
        const auto actualCoverage = GlyphCoverageArea(actual);
        const auto expectedCoverage = GlyphCoverageArea(expected);
        ASSERT_TRUE(actualCoverage);
        ASSERT_TRUE(expectedCoverage);
        ASSERT_GT(*expectedCoverage, 0.0);
        if(fixedHeight)
            EXPECT_LT(*actualCoverage, VectorDoubleGetX(VectorDoubleMultiply(VectorDoubleReplicate(*expectedCoverage), VectorDoubleReplicate(0.999))));
        else
            EXPECT_NEAR(*actualCoverage, *expectedCoverage, VectorDoubleGetX(VectorDoubleMax(VectorDoubleReplicate(0.00001), VectorDoubleMultiply(VectorDoubleReplicate(*expectedCoverage), VectorDoubleReplicate(0.000002)))));
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
            const SIMDVectorSingle regionSize = VectorSubtract(VectorSet(bounds.width, bounds.height, 0.0f, 0.0f), VectorSet(5.0f, 4.0f, 0.0f, 0.0f));
            ExpectGlyphRegion(snapshot, { 25.0f, 24.0f, VectorGetX(regionSize), VectorGetY(regionSize) });
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
    const SIMDVectorSingle regionStart = VectorAdd(VectorSet(sibling->rectangle.x, sibling->rectangle.y, 0.0f, 0.0f), VectorSet(2.0f, 3.0f, 0.0f, 0.0f));
    ExpectGlyphRegion(snapshot, { VectorGetX(regionStart), VectorGetY(regionStart), 44.0f, 24.0f });
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
    const SIMDVectorSingle contentStart = VectorSet(content.x, content.y, 0.0f, 0.0f);
    const SIMDVectorSingle centered = VectorMultiplyAddExpression(VectorSubtract(VectorReplicate(content.height), VectorReplicate(measured->height)), VectorReplicate(0.5f), contentStart);
    // Only y gets the centering offset.
    const SIMDVectorSingle originValue = VectorSubtract(VectorSelect(contentStart, centered, s_SIMDMaskY), VectorSet(measured->x, measured->y, 0.0f, 0.0f));
    const Point origin{ VectorGetX(originValue), VectorGetY(originValue) };
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
            const SIMDVectorSingle titleStart = VectorAdd(VectorReplicate(header.x), VectorReplicate(5.0f));
            const SIMDVectorSingle left = VectorAdd(titleStart, VectorReplicate(collapsible ? 28.0f : 0.0f));
            const SIMDVectorSingle right = VectorMin(VectorReplicate(width), VectorSubtract(VectorAdd(VectorReplicate(header.x), VectorReplicate(header.width)), VectorReplicate(7.0f)));
            const SIMDVectorSingle inkTop = VectorAdd(VectorReplicate(header.y), VectorReplicate(6.0f));
            const SIMDVectorSingle inkHeight = VectorSubtract(VectorReplicate(header.height), VectorReplicate(14.0f));
            ExpectGlyphRegion(snapshot, { VectorGetX(left), VectorGetX(inkTop), VectorGetX(VectorSubtract(right, left)), VectorGetX(inkHeight) });
            const auto skin = sampleSkinPaint(snapshot, Name("window.title"));
            ASSERT_TRUE(skin);
            ExpectRect(skin->bounds, title->clip);
            if(collapsible){
                const HitTarget* collapse = target(id("@window.collapse"));
                ASSERT_NE(collapse, nullptr);
                EXPECT_FLOAT_EQ(collapse->rectangle.x, VectorGetX(titleStart));
                EXPECT_FLOAT_EQ(collapse->rectangle.width, 20.0f);
                EXPECT_LE(VectorGetX(VectorAdd(VectorReplicate(collapse->rectangle.x), VectorReplicate(collapse->rectangle.width))), VectorGetX(left));
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
            EXPECT_FLOAT_EQ(state.placement.bounds.height, VectorGetX(VectorMax(VectorReplicate(32.0f), VectorAdd(VectorAdd(VectorReplicate(measured->height), VectorReplicate(8.0f)), VectorReplicate(8.0f)))));
            EXPECT_FLOAT_EQ(state.placement.scroll, 0.0f);
            const SIMDVectorSingle inkTop = VectorAdd(VectorReplicate(state.placement.textOrigin.y), VectorReplicate(measured->y));
            const SIMDVectorSingle contentBottom = VectorAdd(VectorReplicate(state.placement.content.y), VectorReplicate(state.placement.content.height));
            EXPECT_GE(VectorGetX(inkTop), VectorGetX(VectorSubtract(VectorReplicate(state.placement.content.y), VectorReplicate(0.0001f))));
            EXPECT_LE(VectorGetX(VectorAdd(inkTop, VectorReplicate(measured->height))), VectorGetX(VectorAdd(contentBottom, VectorReplicate(0.0001f))));
            PaintBuilder reference(m_arena);
            reference.begin(display, generation++, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
            ASSERT_TRUE(m_text.paint(reference, *layout, state.placement.textOrigin, m_builder.editStyle().text));
            const auto expectedCoverage = GlyphCoverageArea(reference.freeze());
            const auto actualCoverage = GlyphCoverageArea(actual);
            ASSERT_TRUE(expectedCoverage);
            ASSERT_TRUE(actualCoverage);
            ASSERT_GT(*expectedCoverage, 0.0);
            EXPECT_NEAR(*actualCoverage, *expectedCoverage, VectorDoubleGetX(VectorDoubleMax(VectorDoubleReplicate(0.00001), VectorDoubleMultiply(VectorDoubleReplicate(*expectedCoverage), VectorDoubleReplicate(0.000002)))));
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

