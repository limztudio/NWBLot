// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "list_builder_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/selectable_paint.h>

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_list_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;
using namespace NWB::UiListTests;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiListBuilderTests, SiblingListsPaintTheirOwnDeferredBackgroundStyles){
    m_source.count = 0u;
    ListState secondState;
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel(s_PanelKey, { 10.0f, 10.0f, 320.0f, 260.0f }));
    m_builder.listStyle().background = Name("button.normal");
    ASSERT_TRUE(m_builder.virtualList("first", m_source, m_state, Options()).valid);
    m_builder.listStyle().background = Name("button.hover");
    ASSERT_TRUE(m_builder.virtualList("second", m_source, secondState, Options()).valid);
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    const auto first = skinQuad(snapshot, 6u);
    ASSERT_TRUE(first);
    const auto second = skinQuad(snapshot, 7u);
    ASSERT_TRUE(second);
    EXPECT_LT(first->y, second->y);
}

TEST_F(UiListBuilderTests, DistantTallRowGrowsBeforePaintingAndPreservesItsPixelScrollAnchor){
    m_source.specialIndex = 50000u;
    ASSERT_TRUE(accept(1u));
    ASSERT_NE(target(row(1u)), nullptr);
    const f32 initialHeight = target(row(1u))->rectangle.height;
    const SIMDVectorDouble within = VectorDoubleMultiply(VectorDoubleReplicate(static_cast<f64>(initialHeight)), VectorDoubleReplicate(0.25));
    ASSERT_TRUE(m_state.scrollTo(VectorDoubleGetX(VectorDoubleMultiplyAddExpression(VectorDoubleReplicate(static_cast<f64>(m_source.specialIndex)), VectorDoubleReplicate(static_cast<f64>(initialHeight)), within))));
    m_source.textCalls = 0u;
    m_source.keyCalls = 0u;
    m_source.lookupCalls = 0u;
    ASSERT_TRUE(accept(2u));
    const ScrollPlacement& placement = m_state.placement();
    ASSERT_EQ(placement.firstRow, m_source.specialIndex);
    const HitTarget* tall = target(row(m_source.specialIndex + 1u));
    ASSERT_NE(tall, nullptr);
    const f32 grownHeight = tall->rectangle.height;
    EXPECT_GT(grownHeight, initialHeight);
    const SIMDVectorDouble grown = VectorDoubleReplicate(static_cast<f64>(grownHeight));
    EXPECT_NEAR(tall->rectangle.y, VectorDoubleGetX(VectorDoubleSubtract(VectorDoubleReplicate(static_cast<f64>(placement.viewport.y)), within)), 0.0001);
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), VectorDoubleGetX(VectorDoubleMultiplyAddExpression(VectorDoubleReplicate(static_cast<f64>(m_source.specialIndex)), grown, within)));
    EXPECT_DOUBLE_EQ(placement.contentHeight, VectorDoubleGetX(VectorDoubleMultiply(VectorDoubleReplicate(static_cast<f64>(m_source.count)), grown)));
    const auto metrics = SelectablePainter::Measure(m_skin, m_builder.listStyle().row);
    const auto layout = m_text.layout({ .text = m_source.specialText, .fontSize = m_builder.style().fontSize });
    ASSERT_TRUE(metrics);
    ASSERT_TRUE(layout);
    const auto visual = m_text.measure(*layout, m_paint.displayMetrics());
    ASSERT_TRUE(visual);
    const SIMDVectorSingle grownBound = VectorAdd(VectorReplicate(grownHeight), VectorReplicate(0.0001f));
    const SIMDVectorSingle visualRowHeight = VectorAdd(VectorAdd(VectorReplicate(visual->height), VectorReplicate(metrics->padding.top)), VectorReplicate(metrics->padding.bottom));
    EXPECT_GE(VectorGetX(grownBound), VectorGetX(visualRowHeight));
    const u64 provisionalBound = static_cast<u64>(VectorGetX(VectorCeiling(VectorDivide(VectorReplicate(placement.viewport.height), VectorReplicate(initialHeight))))) + 1u;
    EXPECT_LE(m_source.textCalls, provisionalBound);
    EXPECT_LE(m_source.keyCalls, provisionalBound * 2u + 2u);
    EXPECT_LE(m_source.lookupCalls, provisionalBound + 2u);
    EXPECT_LT(m_context.input().targets().size(), 16u);
    ASSERT_TRUE(m_state.scrollTo(0.0));
    ASSERT_TRUE(accept(3u));
    ASSERT_NE(target(row(1u)), nullptr);
    EXPECT_FLOAT_EQ(target(row(1u))->rectangle.height, grownHeight);
    ++m_source.contentRevision;
    ASSERT_TRUE(accept(4u));
    ASSERT_NE(target(row(1u)), nullptr);
    EXPECT_FLOAT_EQ(target(row(1u))->rectangle.height, initialHeight);
}

TEST_F(UiListBuilderTests, InvisibleTallMaximumIsInvalidatedByFontShapingDisplayAndResolvedRowGeometry){
    Core::Alloc::ScratchArena fontScratchArena(s_FontScratchArenaName);
    const SelectableStyle originalStyle = m_builder.listStyle().row;
    u64 generation = 1u;
    for(u32 change = 0u; change < 7u; ++change){
        SCOPED_TRACE(testing::Message() << "change=" << change);
        m_builder.style().fontSize = 16.0f;
        m_builder.listStyle().row = originalStyle;
        ASSERT_TRUE(m_builder.setTextShaping(TextScriptTag('L', 'a', 't', 'n'), "en"));
        ListState state;
        ArithmeticSource source;
        source.specialIndex = 50000u;
        DisplayMetrics display{ 800.0f, 600.0f, 1.0f, 1.0f };
        ASSERT_TRUE(prepareSource(generation, source, state, Options(), display));
        ASSERT_TRUE(m_context.commitFrame(generation++));
        ASSERT_NE(target(row(1u)), nullptr);
        const f32 seed = target(row(1u))->rectangle.height;
        ASSERT_TRUE(state.scrollTo(VectorDoubleGetX(VectorDoubleMultiply(VectorDoubleReplicate(static_cast<f64>(source.specialIndex)), VectorDoubleReplicate(static_cast<f64>(seed))))));
        ASSERT_TRUE(prepareSource(generation, source, state, Options(), display));
        ASSERT_TRUE(m_context.commitFrame(generation++));
        ASSERT_NE(target(row(source.specialIndex + 1u)), nullptr);
        const f32 grown = target(row(source.specialIndex + 1u))->rectangle.height;
        ASSERT_GT(grown, seed);
        ASSERT_TRUE(state.scrollTo(0.0));
        ASSERT_TRUE(prepareSource(generation, source, state, Options(), display));
        ASSERT_TRUE(m_context.commitFrame(generation++));
        ASSERT_NE(target(row(1u)), nullptr);
        ASSERT_FLOAT_EQ(target(row(1u))->rectangle.height, grown);
        switch(change){
        case 0u: ASSERT_TRUE(UiWidgetTests::InstallLatinFixtureFont(fontScratchArena, m_arena, m_font, m_text)); break;
        case 1u: m_builder.style().fontSize = 12.0f; break;
        case 2u: display.pixelScaleX = 1.25f; display.pixelScaleY = 1.5f; break;
        case 3u: ASSERT_TRUE(m_builder.setTextShaping(TextScriptTag('L', 'a', 't', 'n'), "ko")); break;
        case 4u: m_builder.listStyle().row.padding.bottom = VectorGetX(VectorAdd(VectorReplicate(m_builder.listStyle().row.padding.bottom), VectorReplicate(2.0f))); break;
        case 5u: ++source.generation; break;
        case 6u: --source.count; break;
        }
        source.textCalls = 0u;
        ASSERT_TRUE(prepareSource(generation, source, state, Options(), display));
        ASSERT_TRUE(m_context.commitFrame(generation++));
        ASSERT_NE(target(row(1u)), nullptr);
        const f32 refreshed = target(row(1u))->rectangle.height;
        EXPECT_LT(refreshed, grown);
        const auto metrics = SelectablePainter::Measure(m_skin, m_builder.listStyle().row);
        ASSERT_TRUE(metrics);
        SIMDVectorSingle expected = VectorReplicate(metrics->minimum.y);
        const StringView language = change == 3u ? "ko" : "en";
        for(const StringView label : { StringView{}, StringView{ s_FirstRowText } }){
            const auto layout = m_text.layout({ .text = label, .fontSize = m_builder.style().fontSize, .language = language });
            ASSERT_TRUE(layout);
            const auto visual = m_text.measure(*layout, display);
            ASSERT_TRUE(visual);
            expected = VectorMax(expected, VectorAdd(VectorAdd(VectorReplicate(visual->height), VectorReplicate(metrics->padding.top)), VectorReplicate(metrics->padding.bottom)));
        }
        EXPECT_FLOAT_EQ(refreshed, VectorGetX(expected));
        EXPECT_LE(source.textCalls, 8u);
    }
}

TEST_F(UiListBuilderTests, ReplacingTextServiceWithTheSameGenerationInvalidatesTheCachedMaximum){
    Core::Alloc::ScratchArena fontScratchArena(s_FontScratchArenaName);
    m_source.specialIndex = 50000u;
    ASSERT_TRUE(accept(1u));
    ASSERT_NE(target(row(1u)), nullptr);
    const f32 seed = target(row(1u))->rectangle.height;
    ASSERT_TRUE(m_state.scrollTo(VectorDoubleGetX(VectorDoubleMultiply(VectorDoubleReplicate(static_cast<f64>(m_source.specialIndex)), VectorDoubleReplicate(static_cast<f64>(seed))))));
    ASSERT_TRUE(accept(2u));
    ASSERT_NE(target(row(m_source.specialIndex + 1u)), nullptr);
    const f32 grown = target(row(m_source.specialIndex + 1u))->rectangle.height;
    ASSERT_GT(grown, seed);
    ASSERT_TRUE(m_state.scrollTo(0.0));
    ASSERT_TRUE(accept(3u));
    ASSERT_NE(target(row(1u)), nullptr);
    ASSERT_FLOAT_EQ(target(row(1u))->rectangle.height, grown);
    TextService replacement(m_arena);
    ASSERT_TRUE(UiWidgetTests::InstallLatinFixtureFont(fontScratchArena, m_arena, m_font, replacement));
    ASSERT_EQ(replacement.generation(), m_text.generation());
    ASSERT_NE(replacement.identity(), m_text.identity());
    Builder builder(m_arena, m_context, m_paint, replacement);
    builder.setSkin(m_skin);
    builder.style() = m_builder.style();
    builder.listStyle() = m_builder.listStyle();
    ASSERT_TRUE(begin(4u));
    ASSERT_TRUE(builder.beginPanel(s_PanelKey, { 10.0f, 10.0f, 320.0f, 260.0f }));
    ASSERT_TRUE(builder.virtualList(s_ListKey, m_source, m_state, Options()).valid);
    ASSERT_TRUE(builder.endPanel());
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    ASSERT_TRUE(m_context.commitFrame(4u));
    ASSERT_NE(target(row(1u)), nullptr);
    EXPECT_FLOAT_EQ(target(row(1u))->rectangle.height, seed);
}

TEST_F(UiListBuilderTests, SharedStateDeclarationsMergeSameMetricMaximaWithoutContaminatingDifferentMetrics){
    u64 generation = 1u;
    for(const bool differentMetrics : { false, true }){
        SCOPED_TRACE(testing::Message() << "differentMetrics=" << differentMetrics);
        ArithmeticSource source;
        source.specialIndex = 2u;
        ListState state;
        ASSERT_TRUE(begin(generation));
        ASSERT_TRUE(m_builder.beginPanel(s_PanelKey, { 10.0f, 10.0f, 320.0f, 260.0f }));
        m_builder.style().fontSize = 22.0f;
        ASSERT_TRUE(m_builder.virtualList(s_ListKey, source, state, Options()).valid);
        if(differentMetrics)
            m_builder.style().fontSize = 12.0f;
        ListOptions small = Options();
        small.height.value = 40.0f;
        ASSERT_TRUE(m_builder.virtualList("second", source, state, small).valid);
        ASSERT_TRUE(finishPanel());
        ASSERT_TRUE(m_context.commitFrame(generation++));
        ASSERT_NE(target(row(1u)), nullptr);
        const f32 firstHeight = target(row(1u))->rectangle.height;
        const WidgetId secondRow = UiTestSupport::RequireWidgetId(MakeWidgetPartId(UiTestSupport::RequireWidgetId(MakeWidgetId(id("second", s_PanelKey), s_RowsKey)), 1u));
        ASSERT_NE(target(secondRow), nullptr);
        const f32 secondHeight = target(secondRow)->rectangle.height;
        ASSERT_GT(firstHeight, secondHeight);
        ASSERT_TRUE(state.scrollTo(0.0));
        ASSERT_TRUE(prepareSource(generation, source, state, small));
        ASSERT_TRUE(m_context.commitFrame(generation++));
        ASSERT_NE(target(row(1u)), nullptr);
        EXPECT_FLOAT_EQ(target(row(1u))->rectangle.height, differentMetrics ? secondHeight : firstHeight);
    }
}

TEST_F(UiListBuilderTests, SharedStyledContentKeepsItsFractionalFirstRowAndInheritedClipAcrossFrames){
    m_builder.style().fontSize = 12.0f;
    m_source.specialIndex = 16u;
    ASSERT_TRUE(accept(1u));
    ASSERT_NE(target(row(1u)), nullptr);
    const f32 naturalHeight = target(row(1u))->rectangle.height;
    ASSERT_TRUE(m_state.scrollTo(VectorDoubleGetX(VectorDoubleMultiply(VectorDoubleReplicate(static_cast<f64>(naturalHeight)), VectorDoubleReplicate(10.25)))));
    const WidgetId earlier = UiTestSupport::RequireWidgetId(MakeWidgetId(id("clipped", s_PanelKey), "earlier"));
    const WidgetId earlierRows = UiTestSupport::RequireWidgetId(MakeWidgetId(earlier, s_RowsKey));
    Rect previousRow;
    Rect previousClip;
    for(u64 generation = 2u; generation <= 3u; ++generation){
        ASSERT_TRUE(begin(generation));
        ASSERT_TRUE(m_builder.beginPanel(s_PanelKey, { 10.0f, 10.0f, 320.0f, 260.0f }));
        ContainerOptions clipped;
        clipped.height = { LayoutSizePolicy::Fixed, 20.0f };
        ASSERT_TRUE(m_builder.beginColumn("clipped", clipped));
        m_builder.style().fontSize = 12.0f;
        ListOptions content = Options();
        content.height = { LayoutSizePolicy::Content, 0.0f };
        ASSERT_TRUE(m_builder.virtualList("earlier", m_source, m_state, content).valid);
        ASSERT_TRUE(m_builder.endContainer());
        m_builder.style().fontSize = 22.0f;
        ASSERT_TRUE(m_builder.virtualList("later", m_source, m_state, Options()).valid);
        ASSERT_TRUE(finishPanel());
        ASSERT_TRUE(m_context.commitFrame(generation));
        const HitTarget* hostTarget = target(earlier);
        const HitTarget* first = target(UiTestSupport::RequireWidgetId(MakeWidgetPartId(earlierRows, 11u)));
        ASSERT_NE(hostTarget, nullptr);
        ASSERT_NE(first, nullptr);
        EXPECT_EQ(target(UiTestSupport::RequireWidgetId(MakeWidgetPartId(earlierRows, 10u))), nullptr);
        EXPECT_EQ(target(UiTestSupport::RequireWidgetId(MakeWidgetPartId(earlierRows, 17u))), nullptr);
        EXPECT_FLOAT_EQ(first->rectangle.height, naturalHeight);
        EXPECT_FLOAT_EQ(hostTarget->clip.height, clipped.height.value);
        EXPECT_GT(hostTarget->rectangle.height, hostTarget->clip.height);
        EXPECT_GT(first->clip.height, 0.0f);
        EXPECT_LT(first->clip.height, first->rectangle.height);
        const SIMDVectorSingle clipBottom = VectorAdd(VectorSet(first->clip.y, hostTarget->clip.y, 0.0f, 0.0f), VectorSet(first->clip.height, hostTarget->clip.height, 0.0f, 0.0f));
        EXPECT_FLOAT_EQ(VectorGetX(clipBottom), VectorGetY(clipBottom));
        EXPECT_NEAR(VectorGetX(VectorDivide(VectorSubtract(VectorReplicate(first->clip.y), VectorReplicate(first->rectangle.y)), VectorReplicate(first->rectangle.height))), 0.25f, 0.00001f);
        if(generation != 2u){
            ExpectRect(first->rectangle, previousRow);
            ExpectRect(first->clip, previousClip);
        }
        previousRow = first->rectangle;
        previousClip = first->clip;
    }
}

TEST_F(UiListBuilderTests, ContentViewportPreflightsTallSecondRowAtFractionalScrollBeforeParentLayout){
    m_source.count = 8u;
    m_source.specialIndex = 1u;
    ListOptions options = Options();
    options.height = { LayoutSizePolicy::Content, 0.0f };
    const auto metrics = SelectablePainter::Measure(m_skin, m_builder.listStyle().row);
    const auto ordinary = m_text.layout({ .text = s_FirstRowText, .fontSize = m_builder.style().fontSize });
    const auto tall = m_text.layout({ .text = m_source.specialText, .fontSize = m_builder.style().fontSize });
    ASSERT_TRUE(metrics);
    ASSERT_TRUE(ordinary);
    ASSERT_TRUE(tall);
    const DisplayMetrics display{ 800.0f, 600.0f, 1.0f, 1.0f };
    const auto ordinaryVisual = m_text.measure(*ordinary, display);
    const auto tallVisual = m_text.measure(*tall, display);
    ASSERT_TRUE(ordinaryVisual);
    ASSERT_TRUE(tallVisual);
    // Lanes are the ordinary row and the tall row.
    const SIMDVectorSingle contentHeight = VectorAdd(VectorAdd(VectorSet(ordinaryVisual->height, tallVisual->height, 0.0f, 0.0f), VectorReplicate(metrics->padding.top)), VectorReplicate(metrics->padding.bottom));
    const SIMDVectorSingle rowHeight = VectorMax(VectorReplicate(metrics->minimum.y), contentHeight);
    const f32 initialHeight = VectorGetX(rowHeight);
    const f32 finalHeight = VectorGetY(rowHeight);
    ASSERT_GT(finalHeight, initialHeight);
    ASSERT_TRUE(m_state.scrollTo(VectorDoubleGetX(VectorDoubleMultiply(VectorDoubleReplicate(static_cast<f64>(initialHeight)), VectorDoubleReplicate(0.25)))));
    ASSERT_TRUE(begin(1u, display));
    ASSERT_TRUE(m_builder.beginPanel(s_PanelKey, { 10.0f, 10.0f, 320.0f, 260.0f }));
    ASSERT_TRUE(m_builder.virtualList(s_ListKey, m_source, m_state, options).valid);
    EXPECT_FALSE(m_builder.button("after", ""));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_NE(target(host()), nullptr);
    ASSERT_NE(target(row(1u)), nullptr);
    ASSERT_NE(target(row(2u)), nullptr);
    EXPECT_FLOAT_EQ(target(row(1u))->rectangle.height, finalHeight);
    EXPECT_FLOAT_EQ(target(row(2u))->rectangle.height, finalHeight);
    EXPECT_GE(VectorGetX(VectorAdd(VectorReplicate(m_state.placement().viewport.height), VectorReplicate(0.0001f))), finalHeight);
    const HitTarget* after = target(id("after", s_PanelKey));
    ASSERT_NE(after, nullptr);
    EXPECT_FLOAT_EQ(after->rectangle.y, VectorGetX(VectorAdd(VectorAdd(VectorReplicate(target(host())->rectangle.y), VectorReplicate(target(host())->rectangle.height)), VectorReplicate(m_builder.style().gap))));
    EXPECT_EQ(m_source.textCalls, 2u);
    EXPECT_EQ(m_source.textCallsByIndex[0u], 1u);
    EXPECT_EQ(m_source.textCallsByIndex[1u], 1u);
}

TEST_F(UiListBuilderTests, ScrollingKeepsDisplayedRowsUntilTheMatchingCandidateIsAccepted){
    ASSERT_TRUE(accept(1u));
    ASSERT_NE(target(row(1u)), nullptr);
    const Rect oldBounds = target(row(1u))->rectangle;
    const f64 acceptedStep = target(host())->scrollStep;
    ASSERT_TRUE(wheel());
    ASSERT_TRUE(prepare(2u));
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), acceptedStep);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    ASSERT_NE(target(row(1u)), nullptr);
    EXPECT_FLOAT_EQ(target(row(1u))->rectangle.y, oldBounds.y);
    EXPECT_EQ(m_context.states().entries().size(), 2u);
    const Point oldRow = Center(oldBounds);
    EXPECT_EQ(m_context.input().hitTest(oldRow), row(1u));
    EXPECT_FALSE(m_context.commitFrame(3u));
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(target(row(1u)), nullptr);
    ASSERT_NE(target(row(4u)), nullptr);
    EXPECT_EQ(m_context.input().hitTest(oldRow), row(4u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

