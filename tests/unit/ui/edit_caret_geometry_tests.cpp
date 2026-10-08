// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_caret_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_caret_geometry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiEditCaretTests;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(EditCaretFixture, UnreadyGeometryRejectsEveryQuery){
    EXPECT_FALSE(m_geometry.ready());
    EXPECT_FALSE(m_geometry.caretRect(0u));
    EXPECT_FALSE(m_geometry.hitTest({}));
    EXPECT_FALSE(m_geometry.verticalTarget(0u, true, 0.0f));
    EXPECT_FALSE(m_geometry.rangeOnLine({}, 0u, 1.0f));
}

TEST_F(EditCaretFixture, HardLinesRetainEmptyAndTrailingLineStops){
    ASSERT_TRUE(adoptText("ab\n\nc\n"));
    ASSERT_EQ(m_geometry.lines().size(), 4u);
    ASSERT_EQ(m_geometry.caretStops().size(), 7u);
    const usize begin[]{ 0u, 3u, 4u, 6u };
    const usize end[]{ 2u, 3u, 5u, 6u };
    const usize breakEnd[]{ 3u, 4u, 6u, 6u };
    const u32 first[]{ 0u, 3u, 4u, 6u };
    const u32 count[]{ 3u, 1u, 2u, 1u };
    for(u32 index = 0u; index < 4u; ++index){
        const EditCaretLine& line = m_geometry.lines()[index];
        EXPECT_EQ(line.byteBegin, begin[index]);
        EXPECT_EQ(line.byteEnd, end[index]);
        EXPECT_EQ(line.breakEnd, breakEnd[index]);
        EXPECT_EQ(line.firstStop, first[index]);
        EXPECT_EQ(line.stopCount, count[index]);
    }
    expectCaret(2u, 20.0f, 0.0f);
    expectCaret(3u, 0.0f, 12.0f);
    expectCaret(4u, 0.0f, 24.0f);
    expectCaret(5u, 10.0f, 24.0f);
    expectCaret(6u, 0.0f, 36.0f);
}

TEST_F(EditCaretFixture, LineBreakCaretBelongsToItsPrecedingLineAndFollowingByteStartsTheNext){
    ASSERT_TRUE(adoptText("ab\ncd"));
    expectCaret(2u, 20.0f, 0.0f);
    expectCaret(3u, 0.0f, 12.0f);
    expectCaret(5u, 20.0f, 12.0f);
    EXPECT_EQ(m_geometry.caretStops()[2u].lineIndex, 0u);
    EXPECT_EQ(m_geometry.caretStops()[3u].lineIndex, 1u);
}

TEST_F(EditCaretFixture, MultilineHitTestingChoosesYBeforeXAndKeepsExactMidpointTies){
    ASSERT_TRUE(adoptText("ab\n\nc\n"));
    Expected<usize> byte = MakeUnexpected(Failure{});
    byte = m_geometry.hitTest({ 5.0f, 0.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 0u);
    byte = m_geometry.hitTest({ 5.01f, 0.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 1u);
    byte = m_geometry.hitTest({ 100.0f, 12.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 3u);
    byte = m_geometry.hitTest({ 5.0f, 24.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 4u);
    byte = m_geometry.hitTest({ 5.01f, 24.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 5u);
    byte = m_geometry.hitTest({ -100.0f, -100.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 0u);
    byte = m_geometry.hitTest({ 100.0f, 100.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 6u);
}

TEST_F(EditCaretFixture, SingleLineLigatureInterpolationAndTieBehaviorRemainUnchanged){
    ASSERT_TRUE(adoptText("ffi", EditTextMode::SingleLine));
    ASSERT_EQ(m_geometry.layout().clusters().size(), 1u);
    ASSERT_EQ(m_geometry.caretStops().size(), 4u);
    Expected<usize> byte = MakeUnexpected(Failure{});
    byte = m_geometry.hitTest({ 15.0f, -999.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 1u);
    byte = m_geometry.hitTest({ 15.01f, 999.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 2u);
}

TEST_F(EditCaretFixture, CombiningHitsStayOnGraphemesWhileScalarCaretQueriesRemainAvailable){
    ASSERT_TRUE(adoptText("A\xcc\x81" "B", EditTextMode::SingleLine));
    ASSERT_EQ(m_geometry.caretStops().size(), 3u);
    EXPECT_EQ(m_geometry.caretStops()[1u].committedByte, 3u);
    expectCaret(1u, 5.0f, 0.0f);
    expectCaret(3u, 10.0f, 0.0f);
    Expected<usize> byte = MakeUnexpected(Failure{});
    byte = m_geometry.hitTest({ 5.0f, 0.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 0u);
    byte = m_geometry.hitTest({ 5.01f, 0.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 3u);
    EXPECT_FALSE(m_geometry.caretRect(2u));
}

TEST_F(EditCaretFixture, CollapsedPreeditRetainsBothMappedEdgesAndHangulScalarInterpolation){
    m_mapping.push_back({ 0u, 0u });
    m_mapping.push_back({ 0u, 9u });
    ASSERT_TRUE(adopt("\xe1\x84\x92\xe1\x85\xa1\xe1\x86\xab", 0u, EditTextMode::SingleLine));
    ASSERT_EQ(m_geometry.caretStops().size(), 2u);
    EXPECT_EQ(m_geometry.caretStops()[0u].committedByte, 0u);
    EXPECT_EQ(m_geometry.caretStops()[1u].committedByte, 0u);
    EXPECT_EQ(m_geometry.caretStops()[0u].displayByte, 0u);
    EXPECT_EQ(m_geometry.caretStops()[1u].displayByte, 9u);
    expectCaret(3u, 4.0f, 0.0f);
    expectCaret(6u, 8.0f, 0.0f);
    Expected<usize> byte = MakeUnexpected(Failure{});
    byte = m_geometry.hitTest({ 11.0f, 0.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 0u);
}

TEST_F(EditCaretFixture, PreeditOnlyLinesFallBackToTheClosestCommittedReplacementEdge){
    m_mapping.push_back({ 0u, 0u });
    m_mapping.push_back({ 2u, 4u });
    ASSERT_TRUE(adopt("A\n\nZ", 2u));
    ASSERT_EQ(m_geometry.lines().size(), 3u);
    EXPECT_EQ(m_geometry.lines()[1u].stopCount, 0u);
    EXPECT_EQ(m_geometry.lines()[1u].firstStop, 1u);
    Expected<usize> byte = MakeUnexpected(Failure{});
    byte = m_geometry.hitTest({ 100.0f, 13.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 0u);
    byte = m_geometry.hitTest({ -100.0f, 23.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 2u);
    byte = m_geometry.hitTest({ 0.0f, 18.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 0u);
    byte = m_geometry.hitTest({ 10.0f, 18.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 2u);
}

TEST_F(EditCaretFixture, PreeditOnlyFallbackCannotSelectUnrelatedCommittedPrefixOrSuffixStops){
    const EditCaretMapping entries[]{
        { 0u, 0u }, { 1u, 1u }, { 2u, 2u }, { 3u, 3u }, { 4u, 8u }, { 5u, 9u }, { 6u, 10u }
    };
    m_mapping.assign(entries, entries + 7u);
    ASSERT_TRUE(adopt("ABCX\nY\nZEF", 6u));
    ASSERT_EQ(m_geometry.lines()[1u].stopCount, 0u);
    Expected<usize> byte = MakeUnexpected(Failure{});
    byte = m_geometry.hitTest({ 0.0f, 13.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 3u);
    byte = m_geometry.hitTest({ 999.0f, 13.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 3u);
    byte = m_geometry.hitTest({ 0.0f, 23.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 4u);
    byte = m_geometry.hitTest({ 999.0f, 23.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 4u);
    byte = m_geometry.hitTest({ 0.0f, 18.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 4u);
    byte = m_geometry.hitTest({ 999.0f, 18.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 3u);
    byte = m_geometry.verticalTarget(2u, true, 0.0f);
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 4u);
}

TEST_F(EditCaretFixture, VerticalTargetsRetainTheCallerPreferredColumnAcrossShortLines){
    ASSERT_TRUE(adoptText("abcdef\nx\nabcd"));
    Expected<usize> byte = MakeUnexpected(Failure{});
    byte = m_geometry.verticalTarget(5u, true, 50.0f);
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 8u);
    byte = m_geometry.verticalTarget(*byte, true, 50.0f);
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 13u);
    byte = m_geometry.verticalTarget(*byte, false, 50.0f);
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 8u);
    byte = m_geometry.verticalTarget(*byte, false, 50.0f);
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 5u);
    byte = m_geometry.verticalTarget(0u, true, 5.0f);
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 7u);
    byte = m_geometry.verticalTarget(0u, true, 5.01f);
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 8u);
}

TEST_F(EditCaretFixture, VerticalDocumentBoundaryKeepsTheActiveCaretRatherThanReapplyingPreferredX){
    ASSERT_TRUE(adoptText("abc\nx"));
    Expected<usize> byte = MakeUnexpected(Failure{});
    byte = m_geometry.verticalTarget(1u, false, 999.0f);
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 1u);
    byte = m_geometry.verticalTarget(5u, true, 0.0f);
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 5u);
    ASSERT_TRUE(adoptText("ffi", EditTextMode::SingleLine));
    byte = m_geometry.verticalTarget(1u, true, 0.0f);
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 1u);
}

TEST_F(EditCaretFixture, VerticalMovementCanTargetEmptyAndTrailingLines){
    ASSERT_TRUE(adoptText("ab\n\nc\n"));
    Expected<usize> byte = MakeUnexpected(Failure{});
    byte = m_geometry.verticalTarget(1u, true, 10.0f);
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 3u);
    byte = m_geometry.verticalTarget(*byte, true, 10.0f);
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 5u);
    byte = m_geometry.verticalTarget(*byte, true, 10.0f);
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 6u);
}

TEST_F(EditCaretFixture, EmptyTextAndEmptyCollapsedPreeditHaveValidOwnedGeometry){
    ASSERT_TRUE(adoptText("", EditTextMode::SingleLine));
    ASSERT_EQ(m_geometry.lines().size(), 1u);
    ASSERT_EQ(m_geometry.caretStops().size(), 1u);
    expectCaret(0u, 0.0f, 0.0f);
    m_mapping.push_back({ 0u, 0u });
    ASSERT_TRUE(adopt("", 0u, EditTextMode::SingleLine));
    ASSERT_EQ(m_geometry.caretStops().size(), 2u);
    Expected<usize> byte = MakeUnexpected(Failure{});
    byte = m_geometry.hitTest({ 20.0f, 20.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 0u);
}

TEST_F(EditCaretFixture, MismatchedSourceFailurePreservesBothPriorGeometryAndCandidateOwnership){
    ASSERT_TRUE(adoptText("old\ntext"));
    TextLayout candidate(m_arena);
    {
        auto layoutResult = layout("new");
        ASSERT_TRUE(layoutResult);
        candidate = Move(*layoutResult);
    }
    ASSERT_TRUE(identityMapping("new"));
    EXPECT_FALSE(m_geometry.adoptLayout(Move(candidate), "wrong", m_mapping, 3u, EditTextMode::SingleLine));
    EXPECT_EQ(candidate.utf8(), "new");
    EXPECT_EQ(m_geometry.layout().utf8(), "old\ntext");
    EXPECT_EQ(m_geometry.textMode(), EditTextMode::Multiline);
    EXPECT_TRUE(m_geometry.ready());
    expectCaret(4u, 0.0f, 12.0f);
}

TEST_F(EditCaretFixture, MappingEndpointOrderAndBoundsFailuresPreservePriorGeometry){
    ASSERT_TRUE(adoptText("abc", EditTextMode::SingleLine));
    const EditCaretMapping bad[][4u]{
        { { 1u, 0u }, { 1u, 1u }, { 2u, 2u }, { 3u, 3u } },
        { { 0u, 1u }, { 1u, 1u }, { 2u, 2u }, { 3u, 3u } },
        { { 0u, 0u }, { 1u, 1u }, { 2u, 2u }, { 2u, 3u } },
        { { 0u, 0u }, { 1u, 1u }, { 2u, 2u }, { 3u, 2u } },
        { { 0u, 0u }, { 2u, 1u }, { 1u, 2u }, { 3u, 3u } },
        { { 0u, 0u }, { 1u, 2u }, { 2u, 1u }, { 3u, 3u } },
        { { 0u, 0u }, { 4u, 1u }, { 4u, 2u }, { 3u, 3u } }
    };
    for(const auto& entries : bad){
        m_mapping.assign(entries, entries + 4u);
        EXPECT_FALSE(adopt("abc", 3u, EditTextMode::SingleLine));
        EXPECT_EQ(m_geometry.layout().utf8(), "abc");
        ASSERT_EQ(m_geometry.caretStops().size(), 4u);
        expectCaret(1u, 10.0f, 0.0f);
    }
}

TEST_F(EditCaretFixture, NativeMappingAcceptsScalarEdgesButRejectsContinuationBytes){
    m_mapping.push_back({ 0u, 0u });
    m_mapping.push_back({ 1u, 1u });
    m_mapping.push_back({ 2u, 3u });
    ASSERT_TRUE(adopt("e\xcc\x81", 2u, EditTextMode::SingleLine));
    EXPECT_FLOAT_EQ(m_geometry.caretStops()[1u].x, 5.0f);
    m_mapping[1u].displayByte = 2u;
    EXPECT_FALSE(adopt("e\xcc\x81", 2u, EditTextMode::SingleLine));
    EXPECT_EQ(m_geometry.caretStops()[1u].displayByte, 1u);
}

TEST_F(EditCaretFixture, MultilineRequiresCanonicalLfAndSingleLineRejectsHardBreaks){
    ASSERT_TRUE(adoptText("base", EditTextMode::SingleLine));
    EXPECT_FALSE(adoptText("ab\r\nc", EditTextMode::Multiline));
    EXPECT_FALSE(adoptText("ab\nc", EditTextMode::SingleLine));
    EXPECT_FALSE(adoptText("abc", static_cast<EditTextMode::Enum>(99u)));
    EXPECT_EQ(m_geometry.layout().utf8(), "base");
    EXPECT_EQ(m_geometry.textMode(), EditTextMode::SingleLine);
}

TEST_F(EditCaretFixture, RtlAndMalformedClusterCoverageCannotReplaceReadyLtrGeometry){
    ASSERT_TRUE(adoptText("base", EditTextMode::SingleLine));
    TextLayout rtl(m_arena);
    {
        auto layoutResult = layout("abc", TextDirection::RightToLeft);
        ASSERT_TRUE(layoutResult);
        rtl = Move(*layoutResult);
    }
    ASSERT_TRUE(identityMapping("abc"));
    EXPECT_FALSE(m_geometry.adoptLayout(Move(rtl), "abc", m_mapping, 3u, EditTextMode::SingleLine));
    const ShaperFault::Enum faults[]{ ShaperFault::LeadingGap, ShaperFault::TrailingGap,
        ShaperFault::Overlap, ShaperFault::InconsistentGlyphEnd };
    for(const auto fault : faults){
        SCOPED_TRACE(fault);
        m_shaper.fault = fault;
        TextLayout malformed(m_arena);
        {
            auto layoutResult = layout("abc");
            ASSERT_TRUE(layoutResult);
            malformed = Move(*layoutResult);
        }
        EXPECT_FALSE(m_geometry.adoptLayout(Move(malformed), "abc", m_mapping, 3u, EditTextMode::SingleLine));
        EXPECT_EQ(m_geometry.layout().utf8(), "base");
        expectCaret(1u, 10.0f, 0.0f);
    }
}

TEST_F(EditCaretFixture, NonfiniteDerivedGlyphPlacementCannotReplaceReadyGeometry){
    ASSERT_TRUE(adoptText("base", EditTextMode::SingleLine));
    m_shaper.fault = ShaperFault::NonfinitePlacement;
    TextLayout malformed(m_arena);
    EXPECT_FALSE(layout("ab"));
    EXPECT_TRUE(malformed.glyphs().empty());
    EXPECT_EQ(m_geometry.layout().utf8(), "base");
    expectCaret(1u, 10.0f, 0.0f);
    m_shaper.fault = ShaperFault::None;
    {
        auto layoutResult = layout("ab");
        ASSERT_TRUE(layoutResult);
        malformed = Move(*layoutResult);
    }
    ASSERT_EQ(malformed.glyphs().size(), 2u);
    const_cast<PlacedGlyph&>(malformed.glyphs()[1u]).position.x = Limit<f32>::s_Infinity;
    ASSERT_TRUE(identityMapping("ab"));
    EXPECT_FALSE(m_geometry.adoptLayout(Move(malformed), "ab", m_mapping, 2u, EditTextMode::SingleLine));
    EXPECT_EQ(m_geometry.layout().utf8(), "base");
}

TEST_F(EditCaretFixture, MalformedLineRecordsCannotReplaceReadyGeometry){
    ASSERT_TRUE(adoptText("base", EditTextMode::SingleLine));
    ASSERT_TRUE(identityMapping("a\nb"));
    for(u32 mutation = 0u; mutation < 14u; ++mutation){
        SCOPED_TRACE(mutation);
        TextLayout malformed(m_arena);
        {
            auto layoutResult = layout("a\nb");
            ASSERT_TRUE(layoutResult);
            malformed = Move(*layoutResult);
        }
        TextLine& line = const_cast<TextLine&>(malformed.lines()[1u]);
        switch(mutation){
        case 0u: line.byteBegin = 0u; break;
        case 1u: line.byteEnd = 4u; break;
        case 2u: line.breakEnd = 4u; break;
        case 3u: line.firstCluster = 0u; break;
        case 4u: line.clusterCount = 2u; break;
        case 5u: line.firstGlyph = 0u; break;
        case 6u: line.glyphCount = 2u; break;
        case 7u: line.top = 11.0f; break;
        case 8u: line.height = 0.0f; break;
        case 9u: line.advance = -1.0f; break;
        case 10u: line.baseline = Limit<f32>::s_Infinity; break;
        case 11u: line.height = Limit<f32>::s_QuietNaN; break;
        case 12u: line.advance = 11.0f; break;
        case 13u: const_cast<TextLine&>(malformed.lines()[0u]).breakEnd = 1u; break;
        }
        EXPECT_FALSE(m_geometry.adoptLayout(Move(malformed), "a\nb", m_mapping, 3u, EditTextMode::Multiline));
        EXPECT_EQ(m_geometry.layout().utf8(), "base");
        EXPECT_EQ(m_geometry.textMode(), EditTextMode::SingleLine);
        expectCaret(1u, 10.0f, 0.0f);
    }
}

TEST_F(EditCaretFixture, OwnedGeometrySurvivesSourceAndMappingChangesAndMoves){
    AString<Core::Alloc::GlobalArena> source(m_arena);
    source.assign("ab\nc");
    ASSERT_TRUE(adoptText(AStringView(source)));
    source.assign("replacement");
    m_mapping.clear();
    EditCaretGeometry moved(Move(m_geometry));
    EXPECT_EQ(moved.layout().utf8(), "ab\nc");
    ASSERT_EQ(moved.lines().size(), 2u);
    const auto rectangle = moved.caretRect(4u);
    ASSERT_TRUE(rectangle);
    EXPECT_FLOAT_EQ(rectangle->x, 10.0f);
    EXPECT_FLOAT_EQ(rectangle->y, 12.0f);
    EditCaretGeometry assigned(m_arena);
    assigned = Move(moved);
    Expected<usize> byte = MakeUnexpected(Failure{});
    byte = assigned.hitTest({ 10.0f, 14.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 4u);
}

TEST_F(EditCaretFixture, ZeroAdvanceStopsKeepTheFirstExactTieAndFiniteExtremesStayQueryable){
    m_shaper.zeroAdvance = true;
    ASSERT_TRUE(adoptText("abc", EditTextMode::SingleLine));
    Expected<usize> byte = MakeUnexpected(Failure{});
    byte = m_geometry.hitTest({ 0.0f, 0.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 0u);
    byte = m_geometry.hitTest({ Limit<f32>::s_Max, 0.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 0u);
    byte = m_geometry.hitTest({ -Limit<f32>::s_Max, 0.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 0u);
    m_shaper.zeroAdvance = false;
    ASSERT_TRUE(adoptText("abc", EditTextMode::SingleLine));
    byte = m_geometry.hitTest({ -Limit<f32>::s_Max, 0.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 0u);
    byte = m_geometry.hitTest({ Limit<f32>::s_Max, 0.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 3u);
}

TEST_F(EditCaretFixture, VariableLineHeightsDetermineCaretAndHitBoundaries){
    m_shaper.variedHeight = true;
    ASSERT_TRUE(adoptText("a\nTALL\nb"));
    expectCaret(2u, 0.0f, 12.0f, 20.0f);
    expectCaret(7u, 0.0f, 32.0f);
    Expected<usize> byte = MakeUnexpected(Failure{});
    byte = m_geometry.hitTest({ 0.0f, 31.9f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 2u);
    byte = m_geometry.hitTest({ 0.0f, 32.0f });
    ASSERT_TRUE(byte);
    EXPECT_EQ(*byte, 7u);
}

TEST_F(EditCaretFixture, InvalidCoordinatesAndCaretOffsetsRejectQueries){
    ASSERT_TRUE(adoptText("e\xcc\x81\nx"));
    const Point invalid[]{ { Limit<f32>::s_QuietNaN, 0.0f }, { 0.0f, Limit<f32>::s_Infinity } };
    for(const Point point : invalid)
        EXPECT_FALSE(m_geometry.hitTest(point));
    EXPECT_FALSE(m_geometry.verticalTarget(2u, true, 10.0f));
    EXPECT_FALSE(m_geometry.verticalTarget(0u, true, Limit<f32>::s_QuietNaN));
    EXPECT_FALSE(m_geometry.verticalTarget(6u, true, 10.0f));
    EXPECT_FALSE(m_geometry.caretRect(6u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

