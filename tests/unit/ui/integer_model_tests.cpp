// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "numeric_model_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_integer_model_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::UiNumericTests;
using IntegerModelTests = NumericModelFixture<IntegerEditModel>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(IntegerModelTests, SubmitCommitsExactLargeIntegerAndCancellationCannotRoundIt){
    ASSERT_TRUE(m_model.setDraft("9007199254740993"));
    const NumericEditResult committed = m_model.submit();
    ASSERT_TRUE(committed.valid);
    EXPECT_TRUE(committed.committed);
    EXPECT_TRUE(committed.valueChanged);
    EXPECT_EQ(m_model.value(), 9007199254740993ll);
    EXPECT_EQ(m_model.draft().text(), "9007199254740993");
    ASSERT_TRUE(m_model.setDraft("42"));
    const NumericEditResult cancelled = m_model.cancel();
    ASSERT_TRUE(cancelled.valid);
    EXPECT_TRUE(cancelled.cancelled);
    EXPECT_TRUE(cancelled.restored);
    EXPECT_FALSE(cancelled.committed);
    EXPECT_EQ(m_model.value(), 9007199254740993ll);
    EXPECT_EQ(m_model.draft().text(), "9007199254740993");
}

TEST_F(IntegerModelTests, SubmitPreservesAcceptedLexicalBytesSelectionAndUndoHistory){
    ASSERT_TRUE(m_model.setValue(7));
    EditModel& edit = m_model.lendDraft();
    ASSERT_TRUE(edit.selectAll());
    ASSERT_TRUE(edit.replaceSelection("  +0043 "));
    ASSERT_TRUE(edit.setSelection(2u, 6u));
    ASSERT_TRUE(edit.canUndo());
    const DraftSnapshot before(m_arena, edit);
    const u64 epoch = m_model.revision();
    const NumericEditResult result = m_model.submit();
    ASSERT_TRUE(result.valid);
    EXPECT_TRUE(result.committed);
    EXPECT_TRUE(result.valueChanged);
    EXPECT_FALSE(result.clamped);
    EXPECT_FALSE(result.restored);
    EXPECT_EQ(m_model.value(), 43);
    EXPECT_FALSE(m_model.dirty());
    EXPECT_GT(m_model.revision(), epoch);
    before.expectUnchanged(edit);
    ASSERT_TRUE(edit.undo());
    EXPECT_EQ(edit.text(), "7");
    EXPECT_TRUE(m_model.dirty());
    ASSERT_TRUE(edit.redo());
    EXPECT_EQ(edit.text(), "  +0043 ");
    EXPECT_FALSE(m_model.dirty());
}

TEST_F(IntegerModelTests, EquivalentSubmitIsACommitWithoutRewritingOrChangingTheValue){
    ASSERT_TRUE(m_model.setValue(43));
    ASSERT_TRUE(m_model.setDraft("+00043"));
    const NumericEditResult result = m_model.submit();
    ASSERT_TRUE(result.valid);
    EXPECT_TRUE(result.committed);
    EXPECT_FALSE(result.valueChanged);
    EXPECT_EQ(m_model.draft().text(), "+00043");
    EXPECT_FALSE(m_model.dirty());
    const NumericEditResult cancelled = m_model.cancel();
    ASSERT_TRUE(cancelled.valid);
    EXPECT_TRUE(cancelled.cancelled);
    EXPECT_TRUE(cancelled.restored);
    EXPECT_EQ(m_model.draft().text(), "43");
}

TEST_F(IntegerModelTests, BlurCommitsAndCanonicalizesTheAcceptedDraft){
    ASSERT_TRUE(m_model.setDraft("  +0043 "));
    const u64 external = m_model.draft().externalRevision();
    const NumericEditResult result = m_model.blur();
    ASSERT_TRUE(result.valid);
    EXPECT_TRUE(result.committed);
    EXPECT_TRUE(result.valueChanged);
    EXPECT_FALSE(result.rejected);
    EXPECT_FALSE(result.restored);
    EXPECT_EQ(m_model.value(), 43);
    EXPECT_EQ(m_model.draft().text(), "43");
    EXPECT_EQ(m_model.draft().anchor(), 2u);
    EXPECT_EQ(m_model.draft().caret(), 2u);
    EXPECT_GT(m_model.draft().externalRevision(), external);
    EXPECT_FALSE(m_model.dirty());
    EXPECT_FALSE(m_model.draft().canUndo());
}

TEST_F(IntegerModelTests, InvalidIncompleteAndOverflowSubmitPreserveThePendingDraft){
    ASSERT_TRUE(m_model.setValue(7));
    const AStringView drafts[]{ "-", "x", "9223372036854775808", "-9223372036854775809" };
    for(const AStringView text : drafts){
        SCOPED_TRACE(text);
        ASSERT_TRUE(m_model.setDraft(text));
        ASSERT_TRUE(m_model.lendDraft().setSelection(0u, text.size()));
        const DraftSnapshot before(m_arena, m_model.draft());
        const u64 epoch = m_model.revision();
        const NumericEditResult result = m_model.submit();
        ExpectRejected(result);
        EXPECT_EQ(m_model.value(), 7);
        EXPECT_TRUE(m_model.dirty());
        EXPECT_GT(m_model.revision(), epoch);
        before.expectUnchanged(m_model.draft());
    }
}

TEST_F(IntegerModelTests, InvalidBlurRestoresCanonicalTypedValueInsteadOfNoncanonicalAcceptedBytes){
    ASSERT_TRUE(m_model.setDraft("+0007"));
    ASSERT_TRUE(m_model.submit().committed);
    ASSERT_TRUE(m_model.setDraft("-"));
    const NumericEditResult result = m_model.blur();
    ExpectRejected(result, true);
    EXPECT_EQ(m_model.value(), 7);
    EXPECT_EQ(m_model.draft().text(), "7");
    EXPECT_FALSE(m_model.dirty());
}

TEST_F(IntegerModelTests, BoundsAreInclusiveAndRejectRepresentableValuesOutsideThem){
    const IntegerBounds bounds{ -10, 10 };
    for(const i64 value : { -10ll, 10ll }){
        ASSERT_TRUE(m_model.setValue(value));
        EXPECT_EQ(m_model.status(bounds), NumericParseStatus::Complete);
        EXPECT_TRUE(m_model.submit(bounds).committed);
    }
    ASSERT_TRUE(m_model.setDraft("11"));
    const DraftSnapshot before(m_arena, m_model.draft());
    EXPECT_EQ(m_model.status(bounds), NumericParseStatus::OutOfRange);
    const NumericEditResult result = m_model.submit(bounds);
    ExpectRejected(result);
    EXPECT_EQ(m_model.value(), 10);
    before.expectUnchanged(m_model.draft());
}

TEST_F(IntegerModelTests, ClampingCanonicalizesBothSidesButNeverRepresentationOverflow){
    const IntegerBounds bounds{ -10, 10, NumericBoundsPolicy::Clamp };
    const AStringView drafts[]{ "-11", "+00011" };
    const i64 expected[]{ -10, 10 };
    for(usize index = 0u; index < 2u; ++index){
        ASSERT_TRUE(m_model.setDraft(drafts[index]));
        const u64 external = m_model.draft().externalRevision();
        const NumericEditResult result = m_model.submit(bounds);
        ASSERT_TRUE(result.valid);
        EXPECT_TRUE(result.committed);
        EXPECT_TRUE(result.clamped);
        EXPECT_FALSE(result.rejected);
        EXPECT_FALSE(result.restored);
        EXPECT_EQ(m_model.value(), expected[index]);
        EXPECT_EQ(m_model.draft().text(), index == 0u ? "-10" : "10");
        EXPECT_GT(m_model.draft().externalRevision(), external);
        EXPECT_FALSE(m_model.dirty());
    }
    ASSERT_TRUE(m_model.setDraft("9223372036854775808"));
    const NumericEditResult overflow = m_model.submit(bounds);
    ExpectRejected(overflow);
    EXPECT_EQ(m_model.value(), 10);
    EXPECT_EQ(m_model.draft().text(), "9223372036854775808");
}

TEST_F(IntegerModelTests, ExternalValueRemainsAuthoritativeOutsideUserBounds){
    ASSERT_TRUE(m_model.setValue(100));
    const IntegerBounds bounds{ -10, 10 };
    EXPECT_EQ(m_model.status(bounds), NumericParseStatus::OutOfRange);
    ExpectRejected(m_model.submit(bounds));
    const NumericEditResult blurred = m_model.blur(bounds);
    ExpectRejected(blurred);
    EXPECT_EQ(m_model.value(), 100);
    EXPECT_EQ(m_model.draft().text(), "100");
}

TEST_F(IntegerModelTests, CancelAndAbandonOnlyReportRestoredWhenCanonicalBytesChange){
    const NumericEditResult cleanCancel = m_model.cancel();
    ASSERT_TRUE(cleanCancel.valid);
    EXPECT_TRUE(cleanCancel.cancelled);
    EXPECT_FALSE(cleanCancel.restored);
    ASSERT_TRUE(m_model.setDraft("x"));
    const NumericEditResult abandoned = m_model.abandon();
    ASSERT_TRUE(abandoned.valid);
    EXPECT_TRUE(abandoned.restored);
    EXPECT_FALSE(abandoned.cancelled);
    EXPECT_FALSE(abandoned.committed);
    EXPECT_FALSE(abandoned.rejected);
    EXPECT_EQ(m_model.draft().text(), "0");
    EXPECT_FALSE(m_model.abandon().restored);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

