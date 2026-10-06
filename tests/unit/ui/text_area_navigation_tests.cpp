// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/text_area_navigation.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_area_navigation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiWidgetTests;

class UiTextAreaNavigationTests : public WidgetFixture{
public:
    UiTextAreaNavigationTests()
        : m_model(m_arena, {}, EditTextMode::Multiline)
        , m_view(m_arena)
    {}


protected:
    [[nodiscard]] bool installKoreanFallback(){
        const auto path = ::Path<Core::Alloc::GlobalArena>(m_arena, NWB_TEST_FONT_DIRECTORY) / "korean.font";
        Core::Assets::AssetBytes bytes(m_arena);
        if(!Tests::ReadBundledFontBytes(path, bytes))
            return false;
        Font korean(m_arena, Name("tests/ui/fonts/korean"));
        korean.setFontBytes(Move(bytes));
        if(!korean.validatePayload())
            return false;
        const FontSource sources[]{
            { Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), m_font, 1u },
            { Core::Assets::AssetRef<Font>("tests/ui/fonts/korean"), korean, 1u },
        };
        return m_text.setFonts(sources, LengthOf(sources));
    }

    [[nodiscard]] bool shapeCurrent(){
        return m_view.snapshot(m_model) && m_view.shape(m_text, { {}, 14.0f }) == TextLayoutStatus::Success;
    }

    [[nodiscard]] EditNavigationResult resolve(const EditNavigationDirection::Enum direction, const f32 viewportHeight = 0.0f){
        TextAreaNavigationResolver resolver(m_arena, m_text, m_context, m_state, 14.0f);
        return resolver.resolve(m_model, direction, m_state.navigation().snapshot(), viewportHeight);
    }


protected:
    EditModel m_model;
    TextAreaState m_state;
    EditBoxView m_view;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiTextAreaNavigationTests, ResolverReadsReplacedTextInsteadOfStaleLayout){
    ASSERT_TRUE(m_model.setText("MMMM\nMMMM"));
    ASSERT_TRUE(m_model.setSelection(2u, 2u));
    TextAreaNavigationResolver resolver(m_arena, m_text, m_context, m_state, 14.0f);
    const auto preferred = m_state.navigation().snapshot();
    const auto first = resolver.resolve(m_model, EditNavigationDirection::Down, preferred, 0.0f);
    ASSERT_TRUE(first.resolved);
    EXPECT_EQ(first.committedByte, 7u);
    ASSERT_TRUE(m_model.setText("M\nMMMM\nM"));
    ASSERT_TRUE(m_model.setSelection(0u, 0u));
    const auto second = resolver.resolve(m_model, EditNavigationDirection::Down, preferred, 0.0f);
    ASSERT_TRUE(second.resolved);
    EXPECT_EQ(second.committedByte, 2u);
    EXPECT_EQ(m_model.caret(), 0u);
}

TEST_F(UiTextAreaNavigationTests, AdjacentNavigationReachesEmptyAndTrailingHardLines){
    ASSERT_TRUE(m_model.setText("MM\n\nMM\n"));
    ASSERT_TRUE(m_model.setSelection(0u, 0u));
    const auto empty = resolve(EditNavigationDirection::Down);
    ASSERT_TRUE(empty.resolved);
    EXPECT_EQ(empty.committedByte, 3u);
    ASSERT_TRUE(m_model.setSelection(3u, 3u));
    const auto next = resolve(EditNavigationDirection::Down);
    ASSERT_TRUE(next.resolved);
    EXPECT_EQ(next.committedByte, 4u);
    ASSERT_TRUE(m_model.setSelection(7u, 7u));
    const auto previous = resolve(EditNavigationDirection::Up);
    ASSERT_TRUE(previous.resolved);
    EXPECT_EQ(previous.committedByte, 4u);
    EXPECT_FLOAT_EQ(previous.preferredX, 0.0f);
}

TEST_F(UiTextAreaNavigationTests, VerticalTargetsRemainOnExtendedGraphemeBoundaries){
    ASSERT_TRUE(m_model.setText("e\xcc\x81" "M\ne\xcc\x81" "M"));
    ASSERT_TRUE(m_model.setSelection(3u, 3u));
    const auto result = resolve(EditNavigationDirection::Down);
    ASSERT_TRUE(result.resolved);
    EXPECT_EQ(result.committedByte, 8u);
    EXPECT_TRUE(m_model.setSelection(result.committedByte, result.committedByte));
}

TEST_F(UiTextAreaNavigationTests, LigatureCaretStopsInterpolateIdenticalRows){
    ASSERT_TRUE(m_model.setText("ffi\nffi"));
    ASSERT_TRUE(m_model.setSelection(1u, 1u));
    const auto result = resolve(EditNavigationDirection::Down);
    ASSERT_TRUE(result.resolved);
    EXPECT_EQ(result.committedByte, 5u);
    EXPECT_GT(result.preferredX, 0.0f);
}

TEST_F(UiTextAreaNavigationTests, AdjacentDocumentBoundariesPreserveTheActiveCaret){
    ASSERT_TRUE(m_model.setText("MMMM\nM"));
    ASSERT_TRUE(m_model.setSelection(2u, 2u));
    const auto first = resolve(EditNavigationDirection::Up);
    ASSERT_TRUE(first.resolved);
    EXPECT_EQ(first.committedByte, 2u);
    ASSERT_TRUE(m_model.setSelection(6u, 6u));
    const auto last = resolve(EditNavigationDirection::Down);
    ASSERT_TRUE(last.resolved);
    EXPECT_EQ(last.committedByte, 6u);
}

TEST_F(UiTextAreaNavigationTests, PageDocumentBoundariesKeepTheActiveCaret){
    ASSERT_TRUE(m_model.setText("MMMM\nM"));
    ASSERT_TRUE(m_model.setSelection(2u, 2u));
    const auto first = resolve(EditNavigationDirection::PageUp, 1000.0f);
    ASSERT_TRUE(first.resolved);
    EXPECT_EQ(first.committedByte, 2u);
    ASSERT_TRUE(m_model.setSelection(5u, 6u));
    const auto last = resolve(EditNavigationDirection::PageDown, 1000.0f);
    ASSERT_TRUE(last.resolved);
    EXPECT_EQ(last.committedByte, 6u);
    EXPECT_EQ(m_model.anchor(), 5u);
    EXPECT_EQ(m_model.caret(), 6u);
}

TEST_F(UiTextAreaNavigationTests, APageWithinTheCurrentRowKeepsTheActiveCaret){
    ASSERT_TRUE(m_model.setText("MMMM\nMMMM\nMMMM"));
    ASSERT_TRUE(m_model.setSelection(7u, 7u));
    ASSERT_TRUE(shapeCurrent());
    const f32 height = m_view.caretGeometry().lines()[1u].height * 0.25f;
    const auto down = resolve(EditNavigationDirection::PageDown, height);
    ASSERT_TRUE(down.resolved);
    EXPECT_EQ(down.committedByte, 7u);
    const auto up = resolve(EditNavigationDirection::PageUp, height);
    ASSERT_TRUE(up.resolved);
    EXPECT_EQ(up.committedByte, 7u);
}

TEST_F(UiTextAreaNavigationTests, FiniteExtremeViewportHeightClampsToDocumentEnds){
    ASSERT_TRUE(m_model.setText("MMMM\nMMMM\nMMMM"));
    ASSERT_TRUE(m_model.setSelection(2u, 2u));
    const auto last = resolve(EditNavigationDirection::PageDown, Limit<f32>::s_Max);
    ASSERT_TRUE(last.resolved);
    EXPECT_EQ(last.committedByte, 12u);
    ASSERT_TRUE(m_model.setSelection(12u, 12u));
    const auto first = resolve(EditNavigationDirection::PageUp, Limit<f32>::s_Max);
    ASSERT_TRUE(first.resolved);
    EXPECT_EQ(first.committedByte, 2u);
    EXPECT_TRUE(IsFinite(first.preferredX));
    EXPECT_TRUE(IsFinite(last.preferredX));
}

TEST_F(UiTextAreaNavigationTests, PageReachesTheTrailingEmptyLine){
    ASSERT_TRUE(m_model.setText("MM\nMM\n"));
    ASSERT_TRUE(m_model.setSelection(1u, 1u));
    const auto result = resolve(EditNavigationDirection::PageDown, 1000.0f);
    ASSERT_TRUE(result.resolved);
    EXPECT_EQ(result.committedByte, 6u);
    EXPECT_GT(result.preferredX, 0.0f);
}

TEST_F(UiTextAreaNavigationTests, EmptyDocumentResolvesEveryDirectionWithoutMoving){
    const EditNavigationDirection::Enum directions[]{
        EditNavigationDirection::Up, EditNavigationDirection::Down,
        EditNavigationDirection::PageUp, EditNavigationDirection::PageDown
    };
    for(const auto direction : directions){
        const auto result = resolve(direction, 160.0f);
        ASSERT_TRUE(result.resolved);
        EXPECT_EQ(result.committedByte, 0u);
        EXPECT_FLOAT_EQ(result.preferredX, 0.0f);
    }
}

TEST_F(UiTextAreaNavigationTests, ZeroViewportAllowsAdjacentNavigationAndRejectsPages){
    ASSERT_TRUE(m_model.setText("MM\nMM"));
    ASSERT_TRUE(m_model.setSelection(1u, 1u));
    EXPECT_TRUE(resolve(EditNavigationDirection::Down, 0.0f).resolved);
    EXPECT_FALSE(resolve(EditNavigationDirection::PageDown, 0.0f).resolved);
    EXPECT_FALSE(resolve(EditNavigationDirection::PageUp, 0.0f).resolved);
    EXPECT_EQ(m_model.caret(), 1u);
}

TEST_F(UiTextAreaNavigationTests, InvalidViewportValuesPreserveTheBorrowedModelAndState){
    ASSERT_TRUE(m_model.setText("MM\nMM"));
    ASSERT_TRUE(m_model.setSelection(1u, 1u));
    const u64 selection = m_model.selectionGeneration();
    const u64 stateRevision = m_state.revision();
    const f32 heights[]{ -1.0f, Limit<f32>::s_Infinity, -Limit<f32>::s_Infinity, Limit<f32>::s_QuietNaN };
    for(const f32 height : heights){
        EXPECT_FALSE(resolve(EditNavigationDirection::Down, height).resolved);
        EXPECT_FALSE(resolve(EditNavigationDirection::PageUp, height).resolved);
    }
    EXPECT_EQ(m_model.selectionGeneration(), selection);
    EXPECT_EQ(m_model.caret(), 1u);
    EXPECT_EQ(m_state.revision(), stateRevision);
    EXPECT_FALSE(m_state.navigation().hasPreferredX());
}

TEST_F(UiTextAreaNavigationTests, InvalidDirectionAndFontParametersFailClosed){
    ASSERT_TRUE(m_model.setText("MM\nMM"));
    EXPECT_FALSE(resolve(static_cast<EditNavigationDirection::Enum>(255u), 160.0f).resolved);
    const f32 sizes[]{ 0.0f, -1.0f, Limit<f32>::s_Infinity, Limit<f32>::s_QuietNaN };
    for(const f32 size : sizes){
        TextAreaNavigationResolver resolver(m_arena, m_text, m_context, m_state, size);
        EXPECT_FALSE(resolver.resolve(m_model, EditNavigationDirection::Down, m_state.navigation().snapshot(), 0.0f).resolved);
    }
}

TEST_F(UiTextAreaNavigationTests, ForeignAndModifiedNavigationSnapshotsAreRejected){
    ASSERT_TRUE(m_model.setText("MM\nMM"));
    TextAreaNavigationResolver resolver(m_arena, m_text, m_context, m_state, 14.0f);
    TextAreaState foreign;
    EXPECT_FALSE(resolver.resolve(m_model, EditNavigationDirection::Down, foreign.navigation().snapshot(), 0.0f).resolved);
    auto copied = m_state.navigation().snapshot();
    copied.preferredX = 10.0f;
    EXPECT_FALSE(resolver.resolve(m_model, EditNavigationDirection::Down, copied, 0.0f).resolved);
    copied = m_state.navigation().snapshot();
    copied.valid = true;
    EXPECT_FALSE(resolver.resolve(m_model, EditNavigationDirection::Down, copied, 0.0f).resolved);
    copied = m_state.navigation().snapshot();
    copied.preferredX = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(resolver.resolve(m_model, EditNavigationDirection::Down, copied, 0.0f).resolved);
}

TEST_F(UiTextAreaNavigationTests, ActivePreeditIsPreservedAndNotResolved){
    ASSERT_TRUE(m_model.setText("MM\nMM"));
    ASSERT_TRUE(m_model.setSelection(1u, 1u));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("pre\nedit", 3u, 8u));
    const u64 generation = m_model.compositionGeneration();
    EXPECT_FALSE(resolve(EditNavigationDirection::Down, 160.0f).resolved);
    EXPECT_FALSE(resolve(EditNavigationDirection::PageDown, 160.0f).resolved);
    const auto composition = m_model.composition();
    EXPECT_TRUE(composition.active);
    EXPECT_EQ(composition.text, "pre\nedit");
    EXPECT_EQ(composition.anchor, 3u);
    EXPECT_EQ(composition.caret, 8u);
    EXPECT_EQ(m_model.compositionGeneration(), generation);
    EXPECT_FALSE(m_state.navigation().hasPreferredX());
}

TEST_F(UiTextAreaNavigationTests, SingleLineModelsAreRejectedWithoutMutation){
    EditModel model(m_arena);
    ASSERT_TRUE(model.setText("plain"));
    ASSERT_TRUE(model.setSelection(2u, 2u));
    const u64 selection = model.selectionGeneration();
    TextAreaNavigationResolver resolver(m_arena, m_text, m_context, m_state, 14.0f);
    EXPECT_FALSE(resolver.resolve(model, EditNavigationDirection::Down, m_state.navigation().snapshot(), 160.0f).resolved);
    EXPECT_EQ(model.text(), "plain");
    EXPECT_EQ(model.caret(), 2u);
    EXPECT_EQ(model.selectionGeneration(), selection);
}

TEST_F(UiTextAreaNavigationTests, IdenticalAcceptedScrollIntentRetiresTheResolver){
    ASSERT_TRUE(m_model.setText("MM\nMM"));
    TextAreaNavigationResolver resolver(m_arena, m_text, m_context, m_state, 14.0f);
    const auto preferred = m_state.navigation().snapshot();
    ASSERT_TRUE(m_state.scrollTo({ 0.0f, 0.0f }));
    EXPECT_TRUE(m_state.navigation().matches(preferred));
    EXPECT_FALSE(resolver.resolve(m_model, EditNavigationDirection::Down, preferred, 160.0f).resolved);
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
}

TEST_F(UiTextAreaNavigationTests, ScrollAwayAndBackCannotReviveTheResolver){
    ASSERT_TRUE(m_model.setText("MM\nMM"));
    TextAreaNavigationResolver resolver(m_arena, m_text, m_context, m_state, 14.0f);
    const auto preferred = m_state.navigation().snapshot();
    ASSERT_TRUE(m_state.scrollTo({ 12.0f, 40.0f }));
    ASSERT_TRUE(m_state.scrollTo({ 0.0f, 0.0f }));
    EXPECT_FALSE(resolver.resolve(m_model, EditNavigationDirection::Down, preferred, 160.0f).resolved);
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
}

TEST_F(UiTextAreaNavigationTests, ResetRetiresAnOtherwiseIdenticalInitialState){
    ASSERT_TRUE(m_model.setText("MM\nMM"));
    TextAreaNavigationResolver resolver(m_arena, m_text, m_context, m_state, 14.0f);
    m_state.reset();
    EXPECT_FALSE(resolver.resolve(m_model, EditNavigationDirection::Down, m_state.navigation().snapshot(), 160.0f).resolved);
    EXPECT_TRUE(resolve(EditNavigationDirection::Down).resolved);
}

TEST_F(UiTextAreaNavigationTests, RejectedScrollIntentPreservesTheResolver){
    ASSERT_TRUE(m_model.setText("MM\nMM"));
    ASSERT_TRUE(m_model.setSelection(1u, 1u));
    TextAreaNavigationResolver resolver(m_arena, m_text, m_context, m_state, 14.0f);
    const auto preferred = m_state.navigation().snapshot();
    EXPECT_FALSE(m_state.scrollTo({ -1.0f, 0.0f }));
    const auto result = resolver.resolve(m_model, EditNavigationDirection::Down, preferred, 160.0f);
    ASSERT_TRUE(result.resolved);
    EXPECT_EQ(result.committedByte, 4u);
}

TEST_F(UiTextAreaNavigationTests, FailedContextRejectsAnOtherwiseCurrentBorrow){
    ASSERT_TRUE(m_model.setText("MM\nMM"));
    TextAreaNavigationResolver resolver(m_arena, m_text, m_context, m_state, 14.0f);
    const u64 revision = m_model.revision();
    const auto preferred = m_state.navigation().snapshot();
    m_context.fail();
    EXPECT_FALSE(resolver.resolve(m_model, EditNavigationDirection::Down, preferred, 160.0f).resolved);
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_TRUE(m_state.navigation().matches(preferred));
}

TEST_F(UiTextAreaNavigationTests, ShapeFailurePreservesModelAndViewportIntent){
    ASSERT_TRUE(m_model.setText("MM\nMM"));
    ASSERT_TRUE(m_model.setSelection(1u, 1u));
    ASSERT_TRUE(m_state.scrollTo({ 5.0f, 20.0f }));
    TextService empty(m_arena);
    TextAreaNavigationResolver resolver(m_arena, empty, m_context, m_state, 14.0f);
    const u64 revision = m_state.revision();
    const u64 selection = m_model.selectionGeneration();
    const auto preferred = m_state.navigation().snapshot();
    EXPECT_FALSE(resolver.resolve(m_model, EditNavigationDirection::Down, preferred, 160.0f).resolved);
    EXPECT_EQ(m_model.text(), "MM\nMM");
    EXPECT_EQ(m_model.caret(), 1u);
    EXPECT_EQ(m_model.selectionGeneration(), selection);
    EXPECT_EQ(m_state.revision(), revision);
    EXPECT_FLOAT_EQ(m_state.scroll().x, 5.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 20.0f);
    EXPECT_TRUE(m_state.navigation().matches(preferred));
}

TEST_F(UiTextAreaNavigationTests, ExplicitHangulPolicyKeepsVerticalTargetAlignedWithVisibleJamo){
    ASSERT_TRUE(installKoreanFallback());
    ASSERT_TRUE(m_model.setText("\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB\naaa"));
    ASSERT_TRUE(m_model.setSelection(9u, 9u));
    const ShapeRequest request{ {}, 14.0f, TextDirection::LeftToRight, TextScriptTag('H', 'a', 'n', 'g'), "ko" };
    EditBoxView visual(m_arena);
    ASSERT_TRUE(visual.snapshot(m_model));
    ASSERT_EQ(visual.shape(m_text, request), TextLayoutStatus::Success);
    Rect caret;
    ASSERT_TRUE(visual.caretGeometry().caretRect(visual.displayCaret(), caret));
    usize expected = 0u;
    ASSERT_TRUE(visual.caretGeometry().verticalTarget(visual.displayCaret(), true, caret.x, expected));

    const auto preferred = m_state.navigation().snapshot();
    AString<Core::Alloc::GlobalArena> language("ko", m_arena);
    TextAreaNavigationResolver configured(m_arena, m_text, m_context, m_state, 14.0f,
        TextScriptTag('H', 'a', 'n', 'g'), StringView(language.data(), language.size()));
    language.assign("!!", 2u);
    const EditNavigationResult matching = configured.resolve(m_model, EditNavigationDirection::Down, preferred, 0.0f);
    ASSERT_TRUE(matching.resolved);
    EXPECT_EQ(matching.committedByte, expected);
    TextAreaNavigationResolver defaultPolicy(m_arena, m_text, m_context, m_state, 14.0f);
    const EditNavigationResult different = defaultPolicy.resolve(m_model, EditNavigationDirection::Down, preferred, 0.0f);
    ASSERT_TRUE(different.resolved);
    EXPECT_NE(matching.committedByte, different.committedByte);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

