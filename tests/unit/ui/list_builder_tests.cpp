// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/selectable_paint.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_list_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;


// This source represents a large dataset arithmetically; no allocation or full scan depends on rowCount().
class ArithmeticSource final : public IListDataSource{
public:
    virtual ~ArithmeticSource()override = default;


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{ invalidateText(); return generation; }
    [[nodiscard]] virtual u64 revision()const override{ invalidateText(); return contentRevision; }
    [[nodiscard]] virtual u64 rowCount()const override{ invalidateText(); return count; }

    [[nodiscard]] virtual u64 key(const u64 index)const override{
        invalidateText();
        ++keyCalls;
        if(duplicate)
            return 1u;
        return rawKey(index);
    }

    [[nodiscard]] virtual Expected<u64> indexOf(const u64 value)const override{
        u64 index = 0u;
        invalidateText();
        ++lookupCalls;
        if(malformedLookup){
            index = count;
            return index;
        }
        if(value == 0u || value == removed || value > count + (removed != 0u ? 1u : 0u))
            return MakeUnexpected(Failure{});
        const u64 forward = value - 1u - (removed != 0u && value > removed ? 1u : 0u);
        index = reverse ? count - 1u - forward : forward;
        return index < count ? Expected<u64>{ index } : MakeUnexpected(Failure{});
    }

    [[nodiscard]] virtual Expected<u64> findEnabled(const u64 start, const bool backwards)const noexcept override{
        invalidateText();
        ++searchCalls;
        return UiWidgetTests::FindSingleDisabledFixtureRow(count, start, backwards, disabled, [this](const u64 index)noexcept{ return rawKey(index); });
    }

    [[nodiscard]] virtual StringView text(const u64 index)const override{
        ++textCalls;
        if(index < textCallsByIndex.size())
            ++textCallsByIndex[index];
        if(index == specialIndex){
            for(usize byte = 0u; byte < specialText.size(); ++byte)
                temporary[byte] = specialText[byte];
            return { temporary.data(), specialText.size() };
        }
        temporary[0u] = static_cast<char>('A' + index % 26u);
        temporary[1u] = 'x';
        return { temporary.data(), 2u };
    }

    [[nodiscard]] virtual bool enabled(const u64 index)const noexcept override{
        invalidateText();
        ++enabledCalls;
        return rawKey(index) != disabled;
    }


private:
    [[nodiscard]] u64 rawKey(const u64 index)const noexcept{
        const u64 forward = reverse ? count - 1u - index : index;
        const u64 value = forward + 1u;
        return value + (removed != 0u && value >= removed ? 1u : 0u);
    }

    void invalidateText()const noexcept{
        temporary.fill('?');
    }


public:
    u64 count = 100000u;
    u64 generation = 301u;
    u64 contentRevision = 1u;
    u64 removed = 0u;
    u64 disabled = 0u;
    u64 specialIndex = Limit<u64>::s_Max;
    StringView specialText = "A\xCC\x81\xCC\x81 g\xCC\xA3\nPrevious\nNext";
    bool reverse = false;
    bool duplicate = false;
    bool malformedLookup = false;
    mutable u64 keyCalls = 0u;
    mutable u64 lookupCalls = 0u;
    mutable u64 searchCalls = 0u;
    mutable u64 textCalls = 0u;
    mutable Array<u64, 32u> textCallsByIndex{};
    mutable u64 enabledCalls = 0u;
    mutable Array<char, 128u> temporary{};
};

[[nodiscard]] static ListOptions Options(){
    ListOptions options;
    options.width = { LayoutSizePolicy::Fixed, 280.0f };
    options.height = { LayoutSizePolicy::Fixed, 120.0f };
    return options;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiListBuilderTests : public WidgetFixture{
protected:
    [[nodiscard]] bool prepare(const u64 generation, const ListOptions& options = Options()){
        return prepareSource(generation, m_source, m_state, options);
    }

    [[nodiscard]] bool prepareSource(const u64 generation, const IListDataSource& source, ListState& state, const ListOptions& options = Options(), const DisplayMetrics& display = { 800.0f, 600.0f, 1.0f, 1.0f }){
        if(!begin(generation, display) || !m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }))
            return false;
        m_result = m_builder.virtualList("list", source, state, options);
        return m_result.valid && finishPanel();
    }

    [[nodiscard]] bool accept(const u64 generation, const ListOptions& options = Options()){
        return prepare(generation, options) && m_context.commitFrame(generation);
    }

    [[nodiscard]] bool preparePopups(const u64 generation, PopupState& lowerPopup, ListState& lowerState, PopupState* upperPopup = nullptr, ListState* upperState = nullptr){
        if(!begin(generation) || !m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }))
            return false;
        if(!m_builder.virtualList("list", m_source, m_state, Options()).valid || !m_builder.endPanel())
            return false;
        PopupOptions popup;
        popup.anchor = { 350.0f, 10.0f, 10.0f, 10.0f };
        popup.size = { 320.0f, 180.0f };
        if(m_builder.beginPopup("popup", lowerPopup, popup)){
            if(!m_builder.virtualList("list", m_source, lowerState, Options()).valid || !m_builder.endPopup())
                return false;
        }
        if(upperPopup && upperState){
            popup.anchor = { 400.0f, 220.0f, 10.0f, 10.0f };
            if(m_builder.beginPopup("upper", *upperPopup, popup)){
                if(!m_builder.virtualList("list", m_source, *upperState, Options()).valid || !m_builder.endPopup())
                    return false;
            }
        }
        return m_context.endRoot() && m_context.finishFrame();
    }

    [[nodiscard]] WidgetId host()const{ return id("list", "panel"); }

    [[nodiscard]] WidgetId row(const u64 key)const noexcept{
        return MakeWidgetPartId(MakeWidgetId(host(), "rows"), key);
    }

    [[nodiscard]] bool wheel(const f64 delta = -1.0){
        const HitTarget* accepted = target(host());
        if(!accepted)
            return false;
        InputEvent event;
        event.type = InputEventType::PointerWheel;
        event.position = Center(accepted->rectangle);
        event.scrollY = delta;
        return send(event).pointerConsumed;
    }

    [[nodiscard]] usize glyphIndices(const DrawSnapshot& snapshot)const{
        usize count = 0u;
        for(const auto& command : snapshot.commands()){
            if(command.material == PaintMaterial::Glyph || command.material == PaintMaterial::SdfGlyph)
                count += command.indexCount;
        }
        return count;
    }


protected:
    ArithmeticSource m_source;
    ListState m_state;
    ListResult m_result;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiListBuilderTests, SiblingListsPaintTheirOwnDeferredBackgroundStyles){
    m_source.count = 0u;
    ListState secondState;
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }));
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

TEST_F(UiListBuilderTests, HundredThousandRowsBuildOnlyVisibleTextTargetsAndGlyphs){
    ASSERT_TRUE(prepare(1u));
    const u64 visible = m_state.placement().endRow - m_state.placement().firstRow;
    ASSERT_GT(visible, 0u);
    EXPECT_LE(visible, 6u);
    EXPECT_EQ(m_source.textCalls, visible);
    EXPECT_LE(m_source.keyCalls, visible * 2u + 2u);
    EXPECT_LE(m_source.lookupCalls, visible + 2u);
    EXPECT_EQ(m_source.searchCalls, 0u);
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_EQ(glyphIndices(snapshot), static_cast<usize>(visible) * 12u);
    EXPECT_LT(snapshot.vertices().size(), 200u);
    EXPECT_LT(snapshot.indices().size(), 400u);
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_EQ(m_context.states().entries().size(), 2u);
    EXPECT_EQ(m_context.input().targets().size(), static_cast<usize>(visible) + 4u);
}

TEST_F(UiListBuilderTests, DistantTallRowGrowsBeforePaintingAndPreservesItsPixelScrollAnchor){
    m_source.specialIndex = 50000u;
    ASSERT_TRUE(accept(1u));
    ASSERT_NE(target(row(1u)), nullptr);
    const f32 initialHeight = target(row(1u))->rectangle.height;
    const f64 within = static_cast<f64>(initialHeight) * 0.25;
    ASSERT_TRUE(m_state.scrollTo(static_cast<f64>(m_source.specialIndex) * initialHeight + within));
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
    EXPECT_NEAR(tall->rectangle.y, placement.viewport.y - within, 0.0001);
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), static_cast<f64>(m_source.specialIndex) * grownHeight + within);
    EXPECT_DOUBLE_EQ(placement.contentHeight, static_cast<f64>(m_source.count) * grownHeight);
    const auto metrics = SelectablePainter::Measure(m_skin, m_builder.listStyle().row);
    const auto layout = m_text.layout({ .text = m_source.specialText, .fontSize = m_builder.style().fontSize });
    ASSERT_TRUE(metrics);
    ASSERT_TRUE(layout);
    const auto visual = m_text.measure(*layout, m_paint.displayMetrics());
    ASSERT_TRUE(visual);
    EXPECT_GE(grownHeight + 0.0001f, visual->height + metrics->padding.top + metrics->padding.bottom);
    const u64 provisionalBound = static_cast<u64>(Ceil(placement.viewport.height / initialHeight)) + 1u;
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
        ASSERT_TRUE(state.scrollTo(static_cast<f64>(source.specialIndex) * seed));
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
        case 0u: ASSERT_TRUE(UiWidgetTests::InstallLatinFixtureFont(m_arena, m_font, m_text)); break;
        case 1u: m_builder.style().fontSize = 12.0f; break;
        case 2u: display.pixelScaleX = 1.25f; display.pixelScaleY = 1.5f; break;
        case 3u: ASSERT_TRUE(m_builder.setTextShaping(TextScriptTag('L', 'a', 't', 'n'), "ko")); break;
        case 4u: m_builder.listStyle().row.padding.bottom += 2.0f; break;
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
        f32 expected = metrics->minimum.y;
        const StringView language = change == 3u ? "ko" : "en";
        for(const StringView label : { StringView{}, StringView{ "Ax" } }){
            const auto layout = m_text.layout({ .text = label, .fontSize = m_builder.style().fontSize, .language = language });
            ASSERT_TRUE(layout);
            const auto visual = m_text.measure(*layout, display);
            ASSERT_TRUE(visual);
            expected = Max(expected, visual->height + metrics->padding.top + metrics->padding.bottom);
        }
        EXPECT_FLOAT_EQ(refreshed, expected);
        EXPECT_LE(source.textCalls, 8u);
    }
}

TEST_F(UiListBuilderTests, ReplacingTextServiceWithTheSameGenerationInvalidatesTheCachedMaximum){
    m_source.specialIndex = 50000u;
    ASSERT_TRUE(accept(1u));
    ASSERT_NE(target(row(1u)), nullptr);
    const f32 seed = target(row(1u))->rectangle.height;
    ASSERT_TRUE(m_state.scrollTo(static_cast<f64>(m_source.specialIndex) * seed));
    ASSERT_TRUE(accept(2u));
    ASSERT_NE(target(row(m_source.specialIndex + 1u)), nullptr);
    const f32 grown = target(row(m_source.specialIndex + 1u))->rectangle.height;
    ASSERT_GT(grown, seed);
    ASSERT_TRUE(m_state.scrollTo(0.0));
    ASSERT_TRUE(accept(3u));
    ASSERT_NE(target(row(1u)), nullptr);
    ASSERT_FLOAT_EQ(target(row(1u))->rectangle.height, grown);
    TextService replacement(m_arena);
    ASSERT_TRUE(UiWidgetTests::InstallLatinFixtureFont(m_arena, m_font, replacement));
    ASSERT_EQ(replacement.generation(), m_text.generation());
    ASSERT_NE(replacement.identity(), m_text.identity());
    Builder builder(m_arena, m_context, m_paint, replacement);
    builder.setSkin(m_skin);
    builder.style() = m_builder.style();
    builder.listStyle() = m_builder.listStyle();
    ASSERT_TRUE(begin(4u));
    ASSERT_TRUE(builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }));
    ASSERT_TRUE(builder.virtualList("list", m_source, m_state, Options()).valid);
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
        ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }));
        m_builder.style().fontSize = 22.0f;
        ASSERT_TRUE(m_builder.virtualList("list", source, state, Options()).valid);
        if(differentMetrics)
            m_builder.style().fontSize = 12.0f;
        ListOptions small = Options();
        small.height.value = 40.0f;
        ASSERT_TRUE(m_builder.virtualList("second", source, state, small).valid);
        ASSERT_TRUE(finishPanel());
        ASSERT_TRUE(m_context.commitFrame(generation++));
        ASSERT_NE(target(row(1u)), nullptr);
        const f32 firstHeight = target(row(1u))->rectangle.height;
        const WidgetId secondRow = MakeWidgetPartId(MakeWidgetId(id("second", "panel"), "rows"), 1u);
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
    ASSERT_TRUE(m_state.scrollTo(static_cast<f64>(naturalHeight) * 10.25));
    const WidgetId earlier = MakeWidgetId(id("clipped", "panel"), "earlier");
    const WidgetId earlierRows = MakeWidgetId(earlier, "rows");
    Rect previousRow;
    Rect previousClip;
    for(u64 generation = 2u; generation <= 3u; ++generation){
        ASSERT_TRUE(begin(generation));
        ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }));
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
        const HitTarget* first = target(MakeWidgetPartId(earlierRows, 11u));
        ASSERT_NE(hostTarget, nullptr);
        ASSERT_NE(first, nullptr);
        EXPECT_EQ(target(MakeWidgetPartId(earlierRows, 10u)), nullptr);
        EXPECT_EQ(target(MakeWidgetPartId(earlierRows, 17u)), nullptr);
        EXPECT_FLOAT_EQ(first->rectangle.height, naturalHeight);
        EXPECT_FLOAT_EQ(hostTarget->clip.height, clipped.height.value);
        EXPECT_GT(hostTarget->rectangle.height, hostTarget->clip.height);
        EXPECT_GT(first->clip.height, 0.0f);
        EXPECT_LT(first->clip.height, first->rectangle.height);
        EXPECT_FLOAT_EQ(first->clip.y + first->clip.height, hostTarget->clip.y + hostTarget->clip.height);
        EXPECT_NEAR((first->clip.y - first->rectangle.y) / first->rectangle.height, 0.25f, 0.00001f);
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
    const auto ordinary = m_text.layout({ .text = "Ax", .fontSize = m_builder.style().fontSize });
    const auto tall = m_text.layout({ .text = m_source.specialText, .fontSize = m_builder.style().fontSize });
    ASSERT_TRUE(metrics);
    ASSERT_TRUE(ordinary);
    ASSERT_TRUE(tall);
    const DisplayMetrics display{ 800.0f, 600.0f, 1.0f, 1.0f };
    const auto ordinaryVisual = m_text.measure(*ordinary, display);
    const auto tallVisual = m_text.measure(*tall, display);
    ASSERT_TRUE(ordinaryVisual);
    ASSERT_TRUE(tallVisual);
    const f32 initialHeight = Max(metrics->minimum.y, ordinaryVisual->height + metrics->padding.top + metrics->padding.bottom);
    const f32 finalHeight = Max(metrics->minimum.y, tallVisual->height + metrics->padding.top + metrics->padding.bottom);
    ASSERT_GT(finalHeight, initialHeight);
    ASSERT_TRUE(m_state.scrollTo(initialHeight * 0.25));
    ASSERT_TRUE(begin(1u, display));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }));
    ASSERT_TRUE(m_builder.virtualList("list", m_source, m_state, options).valid);
    EXPECT_FALSE(m_builder.button("after", ""));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_NE(target(host()), nullptr);
    ASSERT_NE(target(row(1u)), nullptr);
    ASSERT_NE(target(row(2u)), nullptr);
    EXPECT_FLOAT_EQ(target(row(1u))->rectangle.height, finalHeight);
    EXPECT_FLOAT_EQ(target(row(2u))->rectangle.height, finalHeight);
    EXPECT_GE(m_state.placement().viewport.height + 0.0001f, finalHeight);
    const HitTarget* after = target(id("after", "panel"));
    ASSERT_NE(after, nullptr);
    EXPECT_FLOAT_EQ(after->rectangle.y, target(host())->rectangle.y + target(host())->rectangle.height + m_builder.style().gap);
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

TEST_F(UiListBuilderTests, InitialProgrammaticSelectionSurvivesAndEndNavigatesToVisibleLastKey){
    m_state.select(50000u);
    ASSERT_TRUE(accept(1u));
    EXPECT_EQ(m_state.selectedKey(), 50000u);
    ASSERT_NE(target(row(50000u)), nullptr);
    press(Core::Key::Tab);
    press(Core::Key::End);
    ASSERT_TRUE(prepare(2u));
    EXPECT_EQ(m_state.selectedKey(), 100000u);
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), m_state.placement().maxOffset);
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_NE(target(row(100000u)), nullptr);
    EXPECT_LE(m_source.textCalls, 12u);
}

TEST_F(UiListBuilderTests, UnseenTallCursorIsMeasuredBeforeRevealAndFailedRevisionRefreshRetainsTheRequest){
    m_source.specialIndex = m_source.count - 1u;
    m_state.select(1u);
    ListOptions options = Options();
    options.selectOnNavigate = false;
    ASSERT_TRUE(accept(1u, options));
    ASSERT_NE(target(row(1u)), nullptr);
    const f32 initialHeight = target(row(1u))->rectangle.height;
    press(Core::Key::Tab);
    ASSERT_EQ(m_context.input().focus(), host());
    press(Core::Key::End);
    m_source.textCalls = 0u;
    ASSERT_TRUE(accept(2u, options));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_EQ(m_state.cursorKey(), m_source.count);
    const HitTarget* cursor = target(row(m_source.count));
    ASSERT_NE(cursor, nullptr);
    EXPECT_GT(cursor->rectangle.height, initialHeight);
    EXPECT_GE(cursor->rectangle.y, cursor->clip.y);
    EXPECT_LE(cursor->rectangle.y + cursor->rectangle.height, cursor->clip.y + cursor->clip.height);
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), m_state.placement().maxOffset);
    EXPECT_LE(m_source.textCalls, 8u);
    ASSERT_TRUE(m_state.scrollTo(0.0));
    ASSERT_TRUE(accept(3u, options));
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 0.0);
    EXPECT_EQ(m_state.cursorKey(), m_source.count);
    EXPECT_EQ(target(row(m_source.count)), nullptr);
    ++m_source.contentRevision;
    const f32 fontSize = m_builder.style().fontSize;
    m_builder.style().fontSize = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(prepare(4u, options));
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_EQ(m_state.cursorKey(), m_source.count);
    m_context.abandonFrame();
    m_builder.reset();
    m_builder.style().fontSize = fontSize;
    ASSERT_TRUE(accept(5u, options));
    cursor = target(row(m_source.count));
    ASSERT_NE(cursor, nullptr);
    EXPECT_GE(cursor->rectangle.y, cursor->clip.y);
    EXPECT_LE(cursor->rectangle.y + cursor->rectangle.height, cursor->clip.y + cursor->clip.height);
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), m_state.placement().maxOffset);
}

TEST_F(UiListBuilderTests, ReorderPreservesSelectionByKeyAndRemovalClearsIt){
    m_state.select(50000u);
    ASSERT_TRUE(accept(1u));
    m_source.reverse = true;
    ++m_source.contentRevision;
    ASSERT_TRUE(prepare(2u));
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_EQ(m_state.selectedKey(), 50000u);
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_NE(target(row(50000u)), nullptr);
    m_source.removed = 50000u;
    --m_source.count;
    ++m_source.contentRevision;
    ASSERT_TRUE(prepare(3u));
    EXPECT_TRUE(m_result.selectionChanged);
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_EQ(m_state.cursorKey(), 0u);
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_EQ(target(row(50000u)), nullptr);
}

TEST_F(UiListBuilderTests, ProgrammaticSameSelectionFencesQueuedActivationAndWheel){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    click(Center(target(row(2u))->rectangle));
    ASSERT_TRUE(wheel());
    const u64 inputGeneration = m_state.inputGeneration();
    m_state.select(1u);
    EXPECT_NE(m_state.inputGeneration(), inputGeneration);
    ASSERT_TRUE(prepare(2u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_FALSE(m_result.activated);
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 0.0);
    EXPECT_TRUE(m_context.input().controlActions().empty());
    EXPECT_FALSE(m_context.input().focus().valid());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().focus(), host());
}

TEST_F(UiListBuilderTests, SourceRevisionFencesHeldCaptureAndRestoresHostFocusAtAcceptance){
    ASSERT_TRUE(accept(1u));
    const Point point = Center(target(row(1u))->rectangle);
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, point }).pointerConsumed);
    ++m_source.contentRevision;
    ASSERT_TRUE(prepare(2u));
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, point }).pointerConsumed);
    EXPECT_TRUE(m_context.input().controlActions().empty());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().focus(), host());
    ASSERT_TRUE(prepare(3u));
    EXPECT_FALSE(m_result.activated);
    EXPECT_EQ(m_state.selectedKey(), 0u);
}

TEST_F(UiListBuilderTests, EscapeBeforeReleaseCancelsRowSelectionAndHostSubmission){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    const Point point = Center(target(row(2u))->rectangle);
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, point }).pointerConsumed);
    ASSERT_TRUE(accept(2u));
    EXPECT_FALSE(m_result.activated);
    press(Core::Key::Escape);
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, point }).pointerConsumed);
    ASSERT_TRUE(accept(3u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_FALSE(m_result.activated);
    EXPECT_FALSE(m_result.selectionChanged);
    press(Core::Key::Tab);
    ASSERT_EQ(m_context.input().focus(), host());
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .key = Core::Key::Enter }).commandConsumed);
    ASSERT_TRUE(accept(4u));
    EXPECT_FALSE(m_result.activated);
    press(Core::Key::Escape);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .key = Core::Key::Enter }).commandConsumed);
    ASSERT_TRUE(accept(5u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_FALSE(m_result.activated);
    press(Core::Key::Tab);
    ASSERT_EQ(m_context.input().focus(), host());
    press(Core::Key::Enter);
    ASSERT_TRUE(accept(6u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_TRUE(m_result.activated);
}

TEST_F(UiListBuilderTests, DatasetReplacementWhileSubmitIsHeldCannotSubmitOnRelease){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    press(Core::Key::Tab);
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .key = Core::Key::Enter }).commandConsumed);
    ++m_source.contentRevision;
    ASSERT_TRUE(accept(2u));
    EXPECT_FALSE(m_result.activated);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .key = Core::Key::Enter }).commandConsumed);
    ASSERT_TRUE(accept(3u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_FALSE(m_result.activated);
    press(Core::Key::Enter);
    ASSERT_TRUE(accept(4u));
    EXPECT_TRUE(m_result.activated);
}

TEST_F(UiListBuilderTests, HeldNavigationCannotMutateAnAcceptedReorderedDataset){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    press(Core::Key::Tab);
    InputEvent event;
    event.type = InputEventType::KeyDown;
    event.key = Core::Key::Down;
    EXPECT_TRUE(send(event).commandConsumed);
    m_source.reverse = true;
    ++m_source.contentRevision;
    ASSERT_TRUE(prepare(2u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    ASSERT_TRUE(m_context.commitFrame(2u));
    event.repeat = true;
    EXPECT_TRUE(send(event).commandConsumed);
    ASSERT_TRUE(prepare(3u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_FALSE(m_result.selectionChanged);
    ASSERT_TRUE(m_context.commitFrame(3u));
    event.type = InputEventType::KeyUp;
    EXPECT_TRUE(send(event).commandConsumed);
}

TEST_F(UiListBuilderTests, ReplacementSourceResetsSelectionAndScroll){
    m_state.select(60000u);
    ASSERT_TRUE(accept(1u));
    ASSERT_GT(m_state.scrollOffset(), 0.0);
    ++m_source.generation;
    ASSERT_TRUE(prepare(2u));
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_EQ(m_state.cursorKey(), 0u);
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 0.0);
    EXPECT_TRUE(m_result.selectionChanged);
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_NE(target(row(1u)), nullptr);
}

TEST_F(UiListBuilderTests, ReplacementStateRejectsOldActivationEvenForTheSameSourceAndHost){
    ASSERT_TRUE(accept(1u));
    click(Center(target(row(2u))->rectangle));
    ListState replacement;
    ASSERT_TRUE(prepareSource(2u, m_source, replacement));
    EXPECT_FALSE(m_result.activated);
    EXPECT_EQ(replacement.selectedKey(), 0u);
    EXPECT_TRUE(m_context.input().controlActions().empty());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(target(host())->control.instanceGeneration, replacement.inputGeneration());
}

TEST_F(UiListBuilderTests, DisabledAndOmittedListsRetirePartsWithoutBorrowingQueuedActions){
    ASSERT_TRUE(accept(1u));
    click(Center(target(row(2u))->rectangle));
    ListOptions disabled = Options();
    disabled.enabled = false;
    ASSERT_TRUE(prepare(2u, disabled));
    EXPECT_FALSE(m_result.activated);
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_TRUE(m_context.input().controlActions().empty());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().targets().size(), 2u);
    EXPECT_FALSE(target(host())->enabled);
    ASSERT_TRUE(begin(3u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }));
    ASSERT_TRUE(finishPanel());
    EXPECT_EQ(target(host()), nullptr);
    EXPECT_EQ(m_context.states().entries().size(), 1u);
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_EQ(m_context.input().targets().size(), 1u);
}

TEST_F(UiListBuilderTests, SourceRevisionChangedDuringScopeRejectsTheDeferredLoan){
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }));
    const ListResult result = m_builder.virtualList("list", m_source, m_state, Options());
    ASSERT_TRUE(result.valid);
    ++m_source.contentRevision;
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_FALSE(m_context.ready());
}

TEST_F(UiListBuilderTests, ContentPreflightRowsCannotHideAChangedSourceBeforePaint){
    ListOptions options = Options();
    options.height = { LayoutSizePolicy::Content, 0.0f };
    ASSERT_TRUE(accept(1u, options));
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }));
    m_source.textCalls = 0u;
    ASSERT_TRUE(m_builder.virtualList("list", m_source, m_state, options).valid);
    ASSERT_GT(m_source.textCalls, 0u);
    ++m_source.contentRevision;
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_FALSE(m_context.ready());
}

TEST_F(UiListBuilderTests, DuplicateOrMalformedVisibleKeysRejectTheFrame){
    m_source.duplicate = true;
    EXPECT_FALSE(prepare(1u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_TRUE(m_context.input().targets().empty());
    m_context.abandonFrame();
    m_builder.reset();
    m_source.duplicate = false;
    m_source.malformedLookup = true;
    EXPECT_FALSE(prepare(2u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_TRUE(m_context.input().targets().empty());
}

TEST_F(UiListBuilderTests, EmptyDatasetBuildsOnlyItsHostAndProvidesOneKeyboardStop){
    m_source.count = 0u;
    ASSERT_TRUE(accept(1u));
    EXPECT_EQ(m_source.textCalls, 0u);
    EXPECT_EQ(m_context.input().targets().size(), 2u);
    EXPECT_FALSE(m_state.placement().scrollbarVisible);
    press(Core::Key::Tab);
    press(Core::Key::Down);
    ASSERT_TRUE(prepare(2u));
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_FALSE(m_result.activated);
    EXPECT_FALSE(m_result.selectionChanged);
}

TEST_F(UiListBuilderTests, ThumbUsesItsAcceptedTrackMaximumWhenCandidateHeightChanges){
    ASSERT_TRUE(accept(1u));
    const WidgetId thumb = MakeWidgetId(host(), "scrollbar");
    ASSERT_NE(target(thumb), nullptr);
    const HitTarget accepted = *target(thumb);
    const Point origin = Center(accepted.rectangle);
    const Point destination{ origin.x, origin.y + 10.0f };
    drag(origin, destination);
    ListOptions resized = Options();
    resized.height.value = 200.0f;
    ASSERT_TRUE(prepare(2u, resized));
    const f64 travel = static_cast<f64>(accepted.gestureReference.height) - accepted.rectangle.height;
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 10.0 / travel * accepted.gestureMaximum);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FLOAT_EQ(target(thumb)->gestureReference.height, 190.0f);
}

TEST_F(UiListBuilderTests, LatestThumbMotionFollowsAnInterveningWheelInNativeInputOrder){
    ASSERT_TRUE(accept(1u));
    const HitTarget accepted = *target(MakeWidgetId(host(), "scrollbar"));
    const Point origin = Center(accepted.rectangle);
    const Point destination{ origin.x, accepted.gestureReference.y + accepted.gestureReference.height - accepted.rectangle.height * 0.5f };
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    InputEvent event;
    event.type = InputEventType::PointerWheel;
    event.position = origin;
    event.scrollY = 1.0;
    EXPECT_TRUE(send(event).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PointerMove, destination }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, destination }).pointerConsumed);
    ASSERT_TRUE(prepare(2u));
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), accepted.gestureMaximum);
    EXPECT_EQ(m_state.placement().endRow, 100000u);
}

TEST_F(UiListBuilderTests, EmptyDatasetKeepsABoundedPageCountWithoutReadingText){
    m_source.count = 0u;
    ListOptions options = Options();
    ASSERT_TRUE(accept(1u, options));
    ASSERT_NE(target(host()), nullptr);
    EXPECT_EQ(target(host())->pageRows, 1u);
    EXPECT_EQ(m_source.textCalls, 0u);
}

TEST_F(UiListBuilderTests, SmallDatasetBoundsPageSizeByRows){
    m_source.count = 2u;
    ListOptions options = Options();
    ASSERT_TRUE(accept(1u, options));
    ASSERT_NE(target(host()), nullptr);
    EXPECT_EQ(target(host())->pageRows, 2u);
    EXPECT_EQ(m_source.textCalls, 2u);
    EXPECT_EQ(m_state.placement().endRow, 2u);
}

TEST_F(UiListBuilderTests, PopupListsAutofocusAndRestoreEachCoveredListHost){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    press(Core::Key::Tab);
    EXPECT_EQ(m_context.input().focus(), host());
    PopupState lowerPopup;
    PopupState upperPopup;
    ListState lowerState;
    ListState upperState;
    lowerState.select(2u);
    upperState.select(3u);
    lowerPopup.open();
    ASSERT_TRUE(preparePopups(2u, lowerPopup, lowerState));
    ASSERT_TRUE(m_context.commitFrame(2u));
    const WidgetId lowerHost = id("list", "popup");
    EXPECT_EQ(m_context.input().focus(), lowerHost);
    upperPopup.open();
    ASSERT_TRUE(preparePopups(3u, lowerPopup, lowerState, &upperPopup, &upperState));
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_EQ(m_context.input().focus(), id("list", "upper"));
    press(Core::Key::Down);
    ASSERT_TRUE(preparePopups(4u, lowerPopup, lowerState, &upperPopup, &upperState));
    EXPECT_EQ(upperState.selectedKey(), 4u);
    EXPECT_EQ(lowerState.selectedKey(), 2u);
    EXPECT_EQ(m_state.selectedKey(), 1u);
    ASSERT_TRUE(m_context.commitFrame(4u));
    upperPopup.close();
    ASSERT_TRUE(preparePopups(5u, lowerPopup, lowerState, &upperPopup, &upperState));
    ASSERT_TRUE(m_context.commitFrame(5u));
    EXPECT_EQ(m_context.input().focus(), lowerHost);
    press(Core::Key::Down);
    ASSERT_TRUE(preparePopups(6u, lowerPopup, lowerState, &upperPopup, &upperState));
    EXPECT_EQ(lowerState.selectedKey(), 3u);
    ASSERT_TRUE(m_context.commitFrame(6u));
    lowerPopup.close();
    ASSERT_TRUE(preparePopups(7u, lowerPopup, lowerState));
    ASSERT_TRUE(m_context.commitFrame(7u));
    EXPECT_EQ(m_context.input().focus(), host());
    press(Core::Key::Down);
    ASSERT_TRUE(prepare(8u));
    EXPECT_EQ(m_state.selectedKey(), 2u);
}

TEST_F(UiListBuilderTests, TwoCompletedThumbDragsAreConsumedInOneDeclarationUsingTheirOwnBaseline){
    ASSERT_TRUE(accept(1u));
    const WidgetId thumb = MakeWidgetId(host(), "scrollbar");
    ASSERT_NE(target(thumb), nullptr);
    const HitTarget accepted = *target(thumb);
    const Point origin = Center(accepted.rectangle);
    drag(origin, { origin.x, origin.y + 10.0f });
    drag(origin, { origin.x, origin.y + 20.0f });
    ASSERT_TRUE(prepare(2u));
    const f64 travel = static_cast<f64>(accepted.gestureReference.height) - accepted.rectangle.height;
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 20.0 / travel * accepted.gestureMaximum);
    EXPECT_FALSE(m_context.input().consumePointerGesture(thumb, accepted.declarationGeneration));
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().focus(), host());
}

TEST_F(UiListBuilderTests, FullHeightMinimumThumbFocusesItsHostWithoutStartingAnUnusableGesture){
    ListOptions options = Options();
    options.height.value = 20.0f;
    ASSERT_TRUE(accept(1u, options));
    const WidgetId thumb = MakeWidgetId(host(), "scrollbar");
    ASSERT_NE(target(thumb), nullptr);
    const HitTarget accepted = *target(thumb);
    EXPECT_FLOAT_EQ(m_state.placement().track.height, m_state.placement().thumb.height);
    EXPECT_FALSE(accepted.pointerGesture);
    click(Center(accepted.rectangle));
    EXPECT_EQ(m_context.input().focus(), host());
    EXPECT_FALSE(m_context.input().consumePointerGesture(thumb, accepted.declarationGeneration));
    ASSERT_TRUE(prepare(2u, options));
    EXPECT_TRUE(m_result.valid);
    EXPECT_TRUE(m_result.focused);
    EXPECT_FALSE(m_result.activated);
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 0.0);
    EXPECT_FALSE(m_context.failed());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

