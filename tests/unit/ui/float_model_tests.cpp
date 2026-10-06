// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "numeric_model_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_float_model_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::UiNumericTests;
using FloatModelTests = NumericModelFixture<FloatEditModel>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(FloatModelTests, ExponentDraftStaysUncommittedUntilCompleteSubmit){
    ASSERT_TRUE(m_model.setValue(1.25));
    for(const AStringView text : { AStringView("1.25e"), AStringView("1.25e-") }){
        ASSERT_TRUE(m_model.setDraft(text));
        EXPECT_EQ(m_model.status(), NumericParseStatus::Incomplete);
        ExpectRejected(m_model.submit());
        EXPECT_EQ(m_model.value(), 1.25);
        EXPECT_EQ(m_model.draft().text(), text);
    }
    ASSERT_TRUE(m_model.setDraft("1.25e-1"));
    const NumericEditResult submitted = m_model.submit();
    ASSERT_TRUE(submitted.valid);
    EXPECT_TRUE(submitted.committed);
    EXPECT_TRUE(submitted.valueChanged);
    EXPECT_EQ(BitCast<u64>(m_model.value()), BitCast<u64>(0.125));
    EXPECT_EQ(m_model.draft().text(), "1.25e-1");
    EXPECT_FALSE(m_model.dirty());
}

TEST_F(FloatModelTests, SignedZeroIsPreservedAndItsSignChangeIsReported){
    ASSERT_TRUE(m_model.setDraft("-0e100"));
    const NumericEditResult negative = m_model.submit();
    ASSERT_TRUE(negative.valid);
    EXPECT_TRUE(negative.committed);
    EXPECT_TRUE(negative.valueChanged);
    EXPECT_EQ(BitCast<u64>(m_model.value()), 0x8000000000000000ull);
    ASSERT_TRUE(m_model.blur().committed);
    EXPECT_EQ(m_model.draft().text(), "-0");
    ASSERT_TRUE(m_model.setDraft("0"));
    const NumericEditResult positive = m_model.submit();
    ASSERT_TRUE(positive.valid);
    EXPECT_TRUE(positive.valueChanged);
    EXPECT_EQ(BitCast<u64>(m_model.value()), 0u);
}

TEST_F(FloatModelTests, BoundsClampFiniteRepresentableValuesAndCanonicalize){
    const FloatBounds bounds{ -10.0, 10.0, NumericBoundsPolicy::Clamp };
    ASSERT_TRUE(m_model.setDraft("9.9e1"));
    EXPECT_EQ(m_model.status(bounds), NumericParseStatus::OutOfRange);
    const NumericEditResult high = m_model.submit(bounds);
    ASSERT_TRUE(high.valid);
    EXPECT_TRUE(high.committed);
    EXPECT_TRUE(high.clamped);
    EXPECT_TRUE(high.valueChanged);
    EXPECT_FALSE(high.restored);
    EXPECT_EQ(m_model.value(), 10.0);
    EXPECT_EQ(m_model.draft().text(), "10");
    ASSERT_TRUE(m_model.setDraft("-99"));
    const NumericEditResult low = m_model.blur(bounds);
    ASSERT_TRUE(low.valid);
    EXPECT_TRUE(low.clamped);
    EXPECT_EQ(m_model.value(), -10.0);
    EXPECT_EQ(m_model.draft().text(), "-10");
}

TEST_F(FloatModelTests, RejectionAndInvalidBlurRestoreCanonicalCurrentValue){
    ASSERT_TRUE(m_model.setDraft("+01.250e0"));
    ASSERT_TRUE(m_model.submit().committed);
    const FloatBounds bounds{ -10.0, 10.0 };
    ASSERT_TRUE(m_model.setDraft("99"));
    const DraftSnapshot before(m_arena, m_model.draft());
    ExpectRejected(m_model.submit(bounds));
    before.expectUnchanged(m_model.draft());
    ExpectRejected(m_model.blur(bounds), true);
    EXPECT_EQ(m_model.value(), 1.25);
    EXPECT_EQ(m_model.draft().text(), "1.25");
    EXPECT_FALSE(m_model.dirty());
}

TEST_F(FloatModelTests, OverflowUnderflowAndNonfiniteSpellingsNeverClamp){
    ASSERT_TRUE(m_model.setValue(1.25));
    const FloatBounds bounds{ -10.0, 10.0, NumericBoundsPolicy::Clamp };
    const AStringView drafts[]{ "1e309", "1e-9999", "nan", "-inf", "x", "-" };
    for(const AStringView text : drafts){
        SCOPED_TRACE(text);
        ASSERT_TRUE(m_model.setDraft(text));
        ExpectRejected(m_model.submit(bounds));
        EXPECT_EQ(m_model.value(), 1.25);
        EXPECT_EQ(m_model.draft().text(), text);
        ExpectRejected(m_model.blur(bounds), true);
        EXPECT_EQ(m_model.draft().text(), "1.25");
    }
}

TEST_F(FloatModelTests, NonfiniteExternalAssignmentsPreserveEveryExistingDraftField){
    ASSERT_TRUE(m_model.setValue(1.25));
    ASSERT_TRUE(m_model.lendDraft().replaceSelection("0"));
    ASSERT_TRUE(m_model.lendDraft().setSelection(0u, 1u));
    ASSERT_TRUE(m_model.lendDraft().beginComposition());
    ASSERT_TRUE(m_model.lendDraft().updateComposition("3", 0u, 1u));
    const DraftSnapshot before(m_arena, m_model.draft());
    const u64 epoch = m_model.revision();
    for(const f64 value : { Limit<f64>::s_Infinity, -Limit<f64>::s_Infinity, Limit<f64>::s_QuietNaN }){
        EXPECT_FALSE(m_model.setValue(value));
        EXPECT_EQ(m_model.revision(), epoch);
        EXPECT_EQ(m_model.value(), 1.25);
        before.expectUnchanged(m_model.draft());
    }
}

TEST_F(FloatModelTests, ExtremeSubnormalAndAdjacentUlpValuesRoundtripWithExactBits){
    const u64 patterns[]{
        0x0000000000000001ull, 0x8000000000000001ull, 0x000fffffffffffffull, 0x0010000000000000ull,
        0x3fefffffffffffffull, 0x3ff0000000000001ull,
        0x4340000000000001ull, 0x7fefffffffffffffull, 0xffefffffffffffffull
    };
    for(const u64 bits : patterns){
        SCOPED_TRACE(bits);
        ASSERT_TRUE(m_model.setValue(BitCast<f64>(bits)));
        ASSERT_TRUE(m_model.submit().committed);
        EXPECT_EQ(BitCast<u64>(m_model.value()), bits);
        ASSERT_TRUE(m_model.blur().committed);
        EXPECT_EQ(BitCast<u64>(m_model.value()), bits);
        ASSERT_TRUE(m_model.setDraft("-"));
        ASSERT_TRUE(m_model.cancel().valid);
        f64 parsed = 0.0;
        ASSERT_EQ(ParseFloatDraft(m_model.draft().text(), parsed), NumericParseStatus::Complete);
        EXPECT_EQ(BitCast<u64>(parsed), bits);
    }
}

TEST_F(FloatModelTests, ExternalFiniteValueCanRemainOutsideUserBounds){
    ASSERT_TRUE(m_model.setValue(1000.25));
    const FloatBounds bounds{ -10.0, 10.0 };
    ExpectRejected(m_model.submit(bounds));
    ExpectRejected(m_model.blur(bounds));
    EXPECT_EQ(m_model.value(), 1000.25);
    EXPECT_EQ(m_model.draft().text(), "1000.25");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

