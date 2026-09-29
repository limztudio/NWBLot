// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "multiline_view_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_multiline_view_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiMultilineViewTests;

class UiMultilineViewTests : public MultilineViewFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiMultilineViewTests, SnapshotOwnsMultilineModeTextAndCrossLineSelection){
    ASSERT_TRUE(m_model.setText("ab\ncd"));
    ASSERT_TRUE(m_model.setSelection(1u, 4u));
    ASSERT_TRUE(shapeView());
    EXPECT_EQ(m_view.textMode(), EditTextMode::Multiline);
    EXPECT_EQ(m_view.caretGeometry().textMode(), EditTextMode::Multiline);
    EXPECT_EQ(m_view.displayText(), "ab\ncd");
    EXPECT_EQ(m_view.selectionRange().begin, 1u);
    EXPECT_EQ(m_view.selectionRange().end, 4u);
    EXPECT_EQ(m_view.displayCaret(), 4u);
    const u64 revision = m_view.revision();
    ASSERT_TRUE(m_model.setText("replacement\ntext"));
    EXPECT_EQ(m_view.displayText(), "ab\ncd");
    EXPECT_EQ(m_view.layout().utf8(), "ab\ncd");
    EXPECT_EQ(m_view.revision(), revision);
    EXPECT_EQ(m_view.selectionRange().begin, 1u);
    EXPECT_EQ(m_view.selectionRange().end, 4u);
    ASSERT_TRUE(place());
    ExpectRect(m_placement.caret, { 20.0f, 32.0f, 1.0f, 12.0f });
}

TEST_F(UiMultilineViewTests, NewSnapshotRequiresMatchingLayoutBeforePlacementOrHits){
    ASSERT_TRUE(m_model.setText("old\nview"));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place());
    const EditBoxPlacement previous = m_placement;
    ASSERT_TRUE(m_model.setText("new\ncontent\n"));
    ASSERT_TRUE(m_view.snapshot(m_model));
    EXPECT_FALSE(m_view.ready());
    EXPECT_EQ(m_view.textMode(), EditTextMode::Multiline);
    EXPECT_FALSE(place());
    ExpectPlacement(m_placement, previous);
    usize hit = 77u;
    EXPECT_FALSE(m_view.hitTest({ 10.0f, 20.0f }, previous, hit));
    EXPECT_EQ(hit, 77u);
    TextLayout layout(m_arena);
    ASSERT_EQ(m_layoutBuilder.layout({ m_view.displayText() }, layout), TextLayoutStatus::Success);
    ASSERT_TRUE(m_view.adoptLayout(Move(layout)));
    EXPECT_TRUE(m_view.ready());
    ASSERT_TRUE(place());
    EXPECT_EQ(m_view.layout().lines().size(), 3u);
}

TEST_F(UiMultilineViewTests, EmptyAndTrailingHardLinesKeepDistinctCaretRows){
    ASSERT_TRUE(m_model.setText("ab\n\ncd\n"));
    ASSERT_TRUE(shapeView());
    ASSERT_EQ(m_view.layout().lines().size(), 4u);
    ASSERT_EQ(m_view.caretGeometry().lines().size(), 4u);
    EXPECT_EQ(m_view.caretStops().size(), 8u);
    EXPECT_EQ(m_view.caretGeometry().lines()[1u].byteBegin, 3u);
    EXPECT_EQ(m_view.caretGeometry().lines()[1u].byteEnd, 3u);
    EXPECT_EQ(m_view.caretGeometry().lines()[3u].byteBegin, 7u);
    EXPECT_FLOAT_EQ(m_view.layout().measure().y, 48.0f);
    ASSERT_TRUE(place());
    ExpectRect(m_placement.caret, { 10.0f, 56.0f, 1.0f, 12.0f });
    EXPECT_FLOAT_EQ(m_placement.textOrigin.y, 20.0f);
    ASSERT_TRUE(m_model.setText(""));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place());
    ASSERT_EQ(m_view.caretStops().size(), 1u);
    ASSERT_EQ(m_view.caretGeometry().lines().size(), 1u);
    ExpectRect(m_placement.caret, { 10.0f, 20.0f, 1.0f, 12.0f });
}

TEST_F(UiMultilineViewTests, HitTestingUsesYAndClampsOutsideTheFirstAndLastLine){
    ASSERT_TRUE(m_model.setText("ab\ncd\n"));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place());
    usize hit = 99u;
    ASSERT_TRUE(m_view.hitTest({ 21.0f, 26.0f }, m_placement, hit));
    EXPECT_EQ(hit, 1u);
    ASSERT_TRUE(m_view.hitTest({ 21.0f, 38.0f }, m_placement, hit));
    EXPECT_EQ(hit, 4u);
    ASSERT_TRUE(m_view.hitTest({ 21.0f, 50.0f }, m_placement, hit));
    EXPECT_EQ(hit, 6u);
    ASSERT_TRUE(m_view.hitTest({ 21.0f, -100.0f }, m_placement, hit));
    EXPECT_EQ(hit, 1u);
    ASSERT_TRUE(m_view.hitTest({ 100.0f, 400.0f }, m_placement, hit));
    EXPECT_EQ(hit, 6u);
}

TEST_F(UiMultilineViewTests, LigatureStopsRetainTheirHardLineAndCommittedOffsets){
    ASSERT_TRUE(m_model.setText("ffi\nffi"));
    ASSERT_TRUE(m_model.setSelection(1u, 6u));
    ASSERT_TRUE(shapeView());
    ASSERT_EQ(m_view.layout().clusters().size(), 2u);
    ASSERT_EQ(m_view.caretStops().size(), 8u);
    EXPECT_EQ(m_view.caretStops()[1u].lineIndex, 0u);
    EXPECT_FLOAT_EQ(m_view.caretStops()[1u].x, 10.0f);
    EXPECT_EQ(m_view.caretStops()[5u].lineIndex, 1u);
    EXPECT_EQ(m_view.caretStops()[5u].committedByte, 5u);
    EXPECT_FLOAT_EQ(m_view.caretStops()[5u].x, 10.0f);
    ASSERT_TRUE(place());
    ExpectRect(m_placement.caret, { 30.0f, 32.0f, 1.0f, 12.0f });
    usize hit = 99u;
    ASSERT_TRUE(m_view.hitTest({ 21.0f, 38.0f }, m_placement, hit));
    EXPECT_EQ(hit, 5u);
}

TEST_F(UiMultilineViewTests, CrossLinePreeditKeepsNativeScalarCaretInsideItsGrapheme){
    ASSERT_TRUE(m_model.setText("A\nB\nC"));
    ASSERT_TRUE(m_model.setSelection(2u, 3u));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("x\n\xe1\x84\x92\xe1\x85\xa1\xe1\x86\xab", 0u, 5u));
    ASSERT_TRUE(shapeView());
    EXPECT_EQ(m_model.text(), "A\nB\nC");
    EXPECT_EQ(m_view.displayText(), "A\nx\n\xe1\x84\x92\xe1\x85\xa1\xe1\x86\xab\nC");
    EXPECT_EQ(m_view.replacementRange().begin, 2u);
    EXPECT_EQ(m_view.replacementRange().end, 3u);
    EXPECT_EQ(m_view.preeditRange().begin, 2u);
    EXPECT_EQ(m_view.preeditRange().end, 13u);
    EXPECT_EQ(m_view.selectionRange().begin, 2u);
    EXPECT_EQ(m_view.selectionRange().end, 7u);
    EXPECT_EQ(m_view.displayCaret(), 7u);
    ASSERT_TRUE(place());
    ExpectRect(m_placement.caret, { 14.0f, 44.0f, 1.0f, 12.0f });
    usize hit = 99u;
    ASSERT_TRUE(m_view.hitTest({ 20.0f, 62.0f }, m_placement, hit));
    EXPECT_EQ(hit, 5u);
    m_model.cancelComposition();
    ASSERT_TRUE(m_model.setText("changed"));
    EXPECT_TRUE(m_view.composing());
    EXPECT_EQ(m_view.displayCaret(), 7u);
    ASSERT_TRUE(place());
    ExpectRect(m_placement.caret, { 14.0f, 44.0f, 1.0f, 12.0f });
}

TEST_F(UiMultilineViewTests, BothScrollAxesRevealCaretAndClampAfterDocumentHomeOrShrink){
    ASSERT_TRUE(m_model.setText("abcde\nabcdef\nabcdefg"));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place(20.0f, 12.0f));
    EXPECT_FLOAT_EQ(m_placement.scroll, 51.0f);
    EXPECT_FLOAT_EQ(m_placement.scrollY, 24.0f);
    ExpectRect(m_placement.caret, { 29.0f, 20.0f, 1.0f, 12.0f });
    usize hit = 99u;
    ASSERT_TRUE(m_view.hitTest({ m_placement.caret.x, m_placement.caret.y + 6.0f }, m_placement, hit));
    EXPECT_EQ(hit, m_model.caret());
    const Point previous{ m_placement.scroll, m_placement.scrollY };
    ASSERT_TRUE(m_model.move(EditMove::DocumentHome));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place(20.0f, 12.0f, previous));
    EXPECT_FLOAT_EQ(m_placement.scroll, 0.0f);
    EXPECT_FLOAT_EQ(m_placement.scrollY, 0.0f);
    ASSERT_TRUE(m_model.setText("A"));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place(20.0f, 12.0f, previous));
    EXPECT_FLOAT_EQ(m_placement.scroll, 0.0f);
    EXPECT_FLOAT_EQ(m_placement.scrollY, 0.0f);
}

TEST_F(UiMultilineViewTests, PaddingAndOuterClipIntersectWithoutChangingLogicalLineGeometry){
    ASSERT_TRUE(m_model.setText("ab\ncd"));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(m_view.arrange({ 10.0f, 20.0f, 100.0f, 40.0f }, { 8.0f, 4.0f, 8.0f, 4.0f },
        { 30.0f, 22.0f, 20.0f, 40.0f }, Point{}, m_placement));
    ExpectRect(m_placement.frameClip, { 30.0f, 22.0f, 20.0f, 38.0f });
    ExpectRect(m_placement.content, { 18.0f, 24.0f, 84.0f, 32.0f });
    ExpectRect(m_placement.clip, { 30.0f, 24.0f, 20.0f, 32.0f });
    ExpectRect(m_placement.caret, { 38.0f, 36.0f, 1.0f, 12.0f });
    EXPECT_FLOAT_EQ(m_view.caretGeometry().lines()[1u].top, 12.0f);
}

TEST_F(UiMultilineViewTests, ZeroViewportKeepsFiniteCaretAndScrollAndProducesAnEmptyClip){
    ASSERT_TRUE(m_model.setText("abcde\nabcdef\nabcdefg"));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place(0.0f, 0.0f, { 500.0f, 500.0f }));
    EXPECT_FLOAT_EQ(m_placement.clip.width, 0.0f);
    EXPECT_FLOAT_EQ(m_placement.clip.height, 0.0f);
    EXPECT_FLOAT_EQ(m_placement.scroll, 71.0f);
    EXPECT_FLOAT_EQ(m_placement.scrollY, 36.0f);
    EXPECT_FLOAT_EQ(m_placement.caret.x + m_placement.caret.width, m_placement.content.x);
    EXPECT_FLOAT_EQ(m_placement.caret.y + m_placement.caret.height, m_placement.content.y);
}

TEST_F(UiMultilineViewTests, FailedPlacementAndHitsPreserveAllCallerOutputs){
    ASSERT_TRUE(m_model.setText("abc\ndef"));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place(20.0f, 12.0f));
    const EditBoxPlacement previous = m_placement;
    EXPECT_FALSE(place(20.0f, 12.0f, { 0.0f, Limit<f32>::s_QuietNaN }));
    ExpectPlacement(m_placement, previous);
    EXPECT_FALSE(place(20.0f, 12.0f, { 0.0f, -1.0f }));
    ExpectPlacement(m_placement, previous);
    EXPECT_FALSE(m_view.arrange({ 10.0f, 20.0f, -1.0f, 20.0f }, {}, {}, Point{}, m_placement));
    ExpectPlacement(m_placement, previous);
    EXPECT_FALSE(m_view.arrange(previous.bounds, { 0.0f, -1.0f }, previous.clip, Point{}, m_placement));
    ExpectPlacement(m_placement, previous);
    EXPECT_FALSE(m_view.arrange(previous.bounds, {}, previous.clip, Point{}, m_placement, 0.0f));
    ExpectPlacement(m_placement, previous);
    usize hit = 123u;
    EXPECT_FALSE(m_view.hitTest({ 10.0f, Limit<f32>::s_QuietNaN }, previous, hit));
    EXPECT_EQ(hit, 123u);
    EditBoxPlacement malformed = previous;
    malformed.textOrigin.y = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(m_view.hitTest({ 10.0f, 20.0f }, malformed, hit));
    EXPECT_EQ(hit, 123u);
}

TEST_F(UiMultilineViewTests, FailedLayoutAdoptionRetainsMultilineGeometryAndPlacement){
    ASSERT_TRUE(m_model.setText("ab\ncd"));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place());
    const EditBoxPlacement previous = m_placement;
    TextLayout wrong(m_arena);
    ASSERT_EQ(m_layoutBuilder.layout({ "other\ntext" }, wrong), TextLayoutStatus::Success);
    EXPECT_FALSE(m_view.adoptLayout(Move(wrong)));
    TextLayout rtl(m_arena);
    ShapeRequest request{ m_view.displayText() };
    request.direction = TextDirection::RightToLeft;
    ASSERT_EQ(m_layoutBuilder.layout(request, rtl), TextLayoutStatus::Success);
    EXPECT_FALSE(m_view.adoptLayout(Move(rtl)));
    EXPECT_EQ(m_view.layout().utf8(), "ab\ncd");
    EXPECT_EQ(m_view.caretGeometry().lines().size(), 2u);
    EXPECT_TRUE(m_view.ready());
    ASSERT_TRUE(place());
    ExpectPlacement(m_placement, previous);
    EXPECT_NE(m_view.shape(m_text), TextLayoutStatus::Success);
    ASSERT_TRUE(place());
    ExpectPlacement(m_placement, previous);
}

TEST_F(UiMultilineViewTests, MovingTheViewRetainsOwnedPreeditLineMappingsAndReadyGeometry){
    ASSERT_TRUE(m_model.setText("ab\ncd"));
    ASSERT_TRUE(m_model.setSelection(1u, 4u));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("x\ny", 0u, 3u));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place());
    const EditBoxPlacement previous = m_placement;
    EditBoxView moved(Move(m_view));
    EditBoxView assigned(m_arena);
    assigned = Move(moved);
    m_model.cancelComposition();
    ASSERT_TRUE(m_model.setText("replacement"));
    EXPECT_EQ(assigned.displayText(), "ax\nyd");
    EXPECT_EQ(assigned.layout().utf8(), "ax\nyd");
    EXPECT_EQ(assigned.textMode(), EditTextMode::Multiline);
    EXPECT_TRUE(assigned.composing());
    EXPECT_TRUE(assigned.ready());
    ASSERT_TRUE(assigned.arrange(previous.bounds, {}, previous.clip, Point{}, m_placement));
    ExpectPlacement(m_placement, previous);
    usize hit = 99u;
    ASSERT_TRUE(assigned.hitTest({ 30.0f, 38.0f }, m_placement, hit));
    EXPECT_EQ(hit, 5u);
}

TEST_F(UiMultilineViewTests, PaintOwnsSelectionSegmentsIncludingAnEmptySelectedHardLine){
    ASSERT_TRUE(loadFont());
    ASSERT_TRUE(m_model.setText("ab\n\ncd\n"));
    ASSERT_TRUE(m_model.setSelection(1u, 6u));
    ASSERT_TRUE(m_view.snapshot(m_model));
    ASSERT_EQ(m_view.shape(m_text), TextLayoutStatus::Success);
    ASSERT_TRUE(place(300.0f, 200.0f));
    ASSERT_TRUE(m_model.setText("changed"));
    EditBoxStyle style;
    style.selection = { 1.0f, 0.0f, 0.0f, 1.0f };
    EditBoxPaintFlags flags;
    flags.focused = true;
    flags.caretVisible = false;
    DrawSnapshot snapshot(m_arena);
    ASSERT_TRUE(paintView(snapshot, style, flags));
    PaintVector<Rect> selected(m_arena);
    CollectSolidRects(snapshot, style.selection, selected);
    ASSERT_EQ(selected.size(), 3u);
    for(u32 line = 0u; line < 3u; ++line){
        EXPECT_FLOAT_EQ(selected[line].y, m_placement.textOrigin.y + m_view.caretGeometry().lines()[line].top);
        EXPECT_FLOAT_EQ(selected[line].height, m_view.caretGeometry().lines()[line].height);
    }
    EXPECT_FLOAT_EQ(selected[1u].x, m_placement.textOrigin.x);
    EXPECT_FLOAT_EQ(selected[1u].width, 1.0f);
    EXPECT_FLOAT_EQ(selected[2u].width, m_view.caretGeometry().lines()[2u].advance);
    EXPECT_FALSE(snapshot.glyphPages().empty());
    EXPECT_EQ(m_view.layout().utf8(), "ab\n\ncd\n");
}

TEST_F(UiMultilineViewTests, PaintOwnsPreeditUnderlinesOnEachHardLineAndUsesNativeCaretVisibility){
    ASSERT_TRUE(loadFont());
    ASSERT_TRUE(m_model.setText("A\nB\nC"));
    ASSERT_TRUE(m_model.setSelection(2u, 3u));
    ASSERT_TRUE(m_model.beginComposition());
    ASSERT_TRUE(m_model.updateComposition("x\n\nz", 0u, 4u));
    ASSERT_TRUE(m_view.snapshot(m_model));
    ASSERT_EQ(m_view.shape(m_text), TextLayoutStatus::Success);
    ASSERT_TRUE(place(300.0f, 200.0f));
    m_model.cancelComposition();
    ASSERT_TRUE(m_model.setText("changed"));
    EditBoxStyle style;
    style.preedit = { 0.0f, 1.0f, 0.0f, 1.0f };
    style.caret = { 0.0f, 0.0f, 1.0f, 1.0f };
    EditBoxPaintFlags flags;
    flags.focused = true;
    flags.caretVisible = false;
    flags.preeditCaretVisible = false;
    DrawSnapshot snapshot(m_arena);
    ASSERT_TRUE(paintView(snapshot, style, flags));
    PaintVector<Rect> underlines(m_arena);
    PaintVector<Rect> carets(m_arena);
    CollectSolidRects(snapshot, style.preedit, underlines);
    CollectSolidRects(snapshot, style.caret, carets);
    ASSERT_EQ(underlines.size(), 3u);
    EXPECT_TRUE(carets.empty());
    for(u32 index = 0u; index < 3u; ++index){
        const EditCaretLine& line = m_view.caretGeometry().lines()[index + 1u];
        EXPECT_FLOAT_EQ(underlines[index].y, m_placement.textOrigin.y + line.top + line.height - 1.0f);
        EXPECT_FLOAT_EQ(underlines[index].height, 1.0f);
    }
    EXPECT_FLOAT_EQ(underlines[1u].width, 1.0f);
    flags.preeditCaretVisible = true;
    ASSERT_TRUE(paintView(snapshot, style, flags));
    CollectSolidRects(snapshot, style.caret, carets);
    ASSERT_EQ(carets.size(), 1u);
    ExpectRect(carets[0u], m_placement.caret);
    flags.focused = false;
    ASSERT_TRUE(paintView(snapshot, style, flags));
    CollectSolidRects(snapshot, style.preedit, underlines);
    CollectSolidRects(snapshot, style.caret, carets);
    EXPECT_TRUE(underlines.empty());
    EXPECT_TRUE(carets.empty());
    EXPECT_TRUE(m_view.composing());
}

TEST_F(UiMultilineViewTests, LegacyHorizontalOverloadAndSingleLineCenteringRemainCompatible){
    ASSERT_TRUE(m_model.setText("abc\ndef"));
    ASSERT_TRUE(shapeView());
    ASSERT_TRUE(place(20.0f, 12.0f, { 15.0f, 0.0f }));
    const EditBoxPlacement pointPlacement = m_placement;
    ASSERT_TRUE(m_view.arrange(pointPlacement.bounds, {}, pointPlacement.frameClip, 15.0f, m_placement));
    ExpectPlacement(m_placement, pointPlacement);
    EditModel single(m_arena);
    ASSERT_TRUE(single.setText("abc"));
    ASSERT_TRUE(m_view.snapshot(single));
    TextLayout layout(m_arena);
    ASSERT_EQ(m_layoutBuilder.layout({ m_view.displayText() }, layout), TextLayoutStatus::Success);
    ASSERT_TRUE(m_view.adoptLayout(Move(layout)));
    ASSERT_TRUE(place(100.0f, 40.0f, { 0.0f, 20.0f }));
    EXPECT_EQ(m_view.textMode(), EditTextMode::SingleLine);
    EXPECT_FLOAT_EQ(m_placement.scrollY, 0.0f);
    EXPECT_FLOAT_EQ(m_placement.textOrigin.y, 34.0f);
    ExpectRect(m_placement.caret, { 40.0f, 34.0f, 1.0f, 12.0f });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

