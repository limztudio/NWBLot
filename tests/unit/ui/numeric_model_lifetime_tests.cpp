// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "numeric_model_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_numeric_model_lifetime_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::UiNumericTests;

template<typename Model>
class NumericLifetimeTests : public NumericModelFixture<Model>{};

using ModelTypes = testing::Types<IntegerEditModel, FloatEditModel>;
TYPED_TEST_SUITE(NumericLifetimeTests, ModelTypes);

using IntegerLifetimeTests = NumericModelFixture<IntegerEditModel>;
using FloatLifetimeTests = NumericModelFixture<FloatEditModel>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TYPED_TEST(NumericLifetimeTests, SameValueExternalReplacementFencesHistorySelectionAndComposition){
    auto& model = this->m_model;
    ASSERT_TRUE(model.setValue(7));
    ASSERT_TRUE(model.lendDraft().replaceSelection("8"));
    ASSERT_TRUE(model.lendDraft().setSelection(0u, 1u));
    ASSERT_TRUE(model.lendDraft().beginComposition());
    ASSERT_TRUE(model.lendDraft().updateComposition("9", 0u, 1u));
    const u64 generation = model.draft().instanceGeneration();
    const u64 external = model.draft().externalRevision();
    const u64 epoch = model.revision();
    ASSERT_TRUE(model.setValue(7));
    EXPECT_GT(model.revision(), epoch);
    EXPECT_GT(model.draft().externalRevision(), external);
    EXPECT_EQ(model.draft().instanceGeneration(), generation);
    EXPECT_EQ(model.draft().text(), "7");
    EXPECT_EQ(model.draft().anchor(), 1u);
    EXPECT_EQ(model.draft().caret(), 1u);
    EXPECT_FALSE(model.draft().composition().active);
    EXPECT_FALSE(model.draft().canUndo());
    EXPECT_FALSE(model.dirty());
    const u64 sameTextRevision = model.draft().revision();
    const u64 sameExternal = model.draft().externalRevision();
    const u64 sameEpoch = model.revision();
    ASSERT_TRUE(model.setValue(7));
    EXPECT_EQ(model.draft().revision(), sameTextRevision);
    EXPECT_GT(model.draft().externalRevision(), sameExternal);
    EXPECT_GT(model.revision(), sameEpoch);
}

TYPED_TEST(NumericLifetimeTests, SameTextDraftReplacementFencesWithoutChangingTheCommittedValue){
    auto& model = this->m_model;
    ASSERT_TRUE(model.setValue(7));
    ASSERT_TRUE(model.lendDraft().selectAll());
    ASSERT_TRUE(model.lendDraft().replaceSelection("8"));
    ASSERT_TRUE(model.lendDraft().beginComposition());
    ASSERT_TRUE(model.lendDraft().updateComposition("9", 0u, 1u));
    const u64 revision = model.draft().revision();
    const u64 external = model.draft().externalRevision();
    const u64 epoch = model.revision();
    ASSERT_TRUE(model.setDraft(model.draft().text()));
    EXPECT_EQ(model.draft().revision(), revision);
    EXPECT_GT(model.draft().externalRevision(), external);
    EXPECT_GT(model.revision(), epoch);
    EXPECT_EQ(model.value(), 7);
    EXPECT_EQ(model.draft().text(), "8");
    EXPECT_TRUE(model.dirty());
    EXPECT_FALSE(model.draft().canUndo());
    EXPECT_FALSE(model.draft().composition().active);
}

TYPED_TEST(NumericLifetimeTests, StatusQueriesPreserveEpochHistorySelectionAndPreedit){
    auto& model = this->m_model;
    ASSERT_TRUE(model.setValue(7));
    ASSERT_TRUE(model.lendDraft().replaceSelection("8"));
    ASSERT_TRUE(model.lendDraft().setSelection(0u, 1u));
    ASSERT_TRUE(model.lendDraft().beginComposition());
    ASSERT_TRUE(model.lendDraft().updateComposition("9", 0u, 1u));
    const DraftSnapshot before(this->m_arena, model.draft());
    const u64 epoch = model.revision();
    for(u32 index = 0u; index < 4u; ++index){
        EXPECT_EQ(model.status(), NumericParseStatus::Complete);
        EXPECT_TRUE(model.dirty());
        EXPECT_EQ(model.revision(), epoch);
        before.expectUnchanged(model.draft());
    }
}

TYPED_TEST(NumericLifetimeTests, AcceptedActionsAdvanceTheLoanEpochEvenWithoutAValueChange){
    auto& model = this->m_model;
    u64 epoch = model.revision();
    const NumericEditResult submitted = model.submit();
    ASSERT_TRUE(submitted.valid);
    EXPECT_TRUE(submitted.committed);
    EXPECT_FALSE(submitted.valueChanged);
    EXPECT_GT(model.revision(), epoch);
    epoch = model.revision();
    ASSERT_TRUE(model.cancel().cancelled);
    EXPECT_GT(model.revision(), epoch);
    epoch = model.revision();
    ASSERT_TRUE(model.abandon().valid);
    EXPECT_GT(model.revision(), epoch);
    epoch = model.revision();
    ASSERT_TRUE(model.blur().committed);
    EXPECT_GT(model.revision(), epoch);
    ASSERT_TRUE(model.setDraft("-"));
    epoch = model.revision();
    ExpectRejected(model.submit());
    EXPECT_GT(model.revision(), epoch);
}

TYPED_TEST(NumericLifetimeTests, RestorationDropsPreeditAndUndoWhileUsingCanonicalCommittedValue){
    auto& model = this->m_model;
    for(const u32 action : { 0u, 1u, 2u }){
        ASSERT_TRUE(model.setDraft("  +0007 "));
        ASSERT_TRUE(model.submit().committed);
        ASSERT_TRUE(model.lendDraft().selectAll());
        ASSERT_TRUE(model.lendDraft().replaceSelection("-"));
        ASSERT_TRUE(model.lendDraft().beginComposition());
        ASSERT_TRUE(model.lendDraft().updateComposition("9", 0u, 1u));
        const u64 external = model.draft().externalRevision();
        const NumericEditResult result = action == 0u ? model.cancel() : action == 1u ? model.abandon() : model.blur();
        ASSERT_TRUE(result.valid);
        EXPECT_EQ(result.cancelled, action == 0u);
        EXPECT_EQ(result.rejected, action == 2u);
        EXPECT_TRUE(result.restored);
        EXPECT_FALSE(result.committed);
        EXPECT_FALSE(result.valueChanged);
        EXPECT_EQ(model.value(), 7);
        EXPECT_EQ(model.draft().text(), "7");
        EXPECT_FALSE(model.dirty());
        EXPECT_FALSE(model.draft().composition().active);
        EXPECT_FALSE(model.draft().canUndo());
        EXPECT_FALSE(model.draft().canRedo());
        EXPECT_EQ(model.draft().anchor(), 1u);
        EXPECT_EQ(model.draft().caret(), 1u);
        EXPECT_GT(model.draft().externalRevision(), external);
    }
}

TYPED_TEST(NumericLifetimeTests, OrderedActionsRestoreTheLatestCommittedOrAuthoritativeValue){
    auto& model = this->m_model;
    ASSERT_TRUE(model.setDraft("+0012"));
    ASSERT_TRUE(model.submit().committed);
    ASSERT_TRUE(model.setDraft("17"));
    ASSERT_TRUE(model.cancel().valid);
    EXPECT_EQ(model.value(), 12);
    EXPECT_EQ(model.draft().text(), "12");
    ASSERT_TRUE(model.setDraft("19"));
    ASSERT_TRUE(model.setValue(9));
    ASSERT_TRUE(model.setDraft("15"));
    ASSERT_TRUE(model.abandon().valid);
    EXPECT_EQ(model.value(), 9);
    EXPECT_EQ(model.draft().text(), "9");
    ASSERT_TRUE(model.setDraft("+0008"));
    ASSERT_TRUE(model.blur().committed);
    ASSERT_TRUE(model.setDraft("-"));
    ASSERT_TRUE(model.abandon().valid);
    EXPECT_EQ(model.value(), 8);
    EXPECT_EQ(model.draft().text(), "8");
}

TYPED_TEST(NumericLifetimeTests, CapacityAndInvalidUtf8FailuresPreserveEveryDraftField){
    auto& model = this->m_model;
    char maximum[s_NumericEditMaxBytes];
    for(char& digit : maximum)
        digit = '0';
    maximum[s_NumericEditMaxBytes - 1u] = '7';
    ASSERT_TRUE(model.setDraft({ maximum, s_NumericEditMaxBytes }));
    ASSERT_TRUE(model.submit().committed);
    EXPECT_EQ(model.value(), 7);
    EXPECT_FALSE(model.dirty());
    ASSERT_TRUE(model.lendDraft().setSelection(0u, 1u));
    ASSERT_TRUE(model.lendDraft().beginComposition());
    ASSERT_TRUE(model.lendDraft().updateComposition("9", 0u, 1u));
    const DraftSnapshot before(this->m_arena, model.draft());
    const u64 epoch = model.revision();
    char excessive[s_NumericEditMaxBytes + 1u];
    for(char& digit : excessive)
        digit = '0';
    EXPECT_FALSE(model.setDraft({ excessive, s_NumericEditMaxBytes + 1u }));
    EXPECT_FALSE(model.setDraft("\xC0\xAF"));
    EXPECT_FALSE(model.setDraft("1\n2"));
    EXPECT_FALSE(model.lendDraft().replaceSelection({ excessive, s_NumericEditMaxBytes + 1u }));
    EXPECT_EQ(model.revision(), epoch);
    EXPECT_EQ(model.value(), 7);
    EXPECT_FALSE(model.dirty());
    before.expectUnchanged(model.draft());
}

TYPED_TEST(NumericLifetimeTests, LocalDraftEditsAreObservedByDraftTokensWithoutInventingModelActions){
    auto& model = this->m_model;
    const u64 epoch = model.revision();
    const u64 revision = model.draft().revision();
    const u64 external = model.draft().externalRevision();
    ASSERT_TRUE(model.lendDraft().replaceSelection("7"));
    EXPECT_EQ(model.revision(), epoch);
    EXPECT_GT(model.draft().revision(), revision);
    EXPECT_EQ(model.draft().externalRevision(), external);
    EXPECT_TRUE(model.dirty());
    EXPECT_EQ(model.value(), 0);
}

template<typename Model, typename Bounds>
void ExpectAtomicBadBounds(Core::Alloc::GlobalArena& arena, Model& model, const Bounds& bounds){
    ASSERT_TRUE(model.setValue(7));
    ASSERT_TRUE(model.lendDraft().replaceSelection("8"));
    ASSERT_TRUE(model.lendDraft().setSelection(0u, 1u));
    ASSERT_TRUE(model.lendDraft().beginComposition());
    ASSERT_TRUE(model.lendDraft().updateComposition("9", 0u, 1u));
    const DraftSnapshot before(arena, model.draft());
    const u64 epoch = model.revision();
    const u64 value = BitCast<u64>(model.value());
    EXPECT_FALSE(model.submit(bounds).valid);
    EXPECT_FALSE(model.blur(bounds).valid);
    EXPECT_EQ(model.status(bounds), NumericParseStatus::Invalid);
    EXPECT_EQ(model.revision(), epoch);
    EXPECT_EQ(BitCast<u64>(model.value()), value);
    EXPECT_TRUE(model.dirty());
    before.expectUnchanged(model.draft());
}

TEST_F(IntegerLifetimeTests, ReversedAndUnknownIntegerBoundsAreAtomic){
    ExpectAtomicBadBounds(m_arena, m_model, IntegerBounds{ 10, 1 });
    const auto unknown = static_cast<NumericBoundsPolicy::Enum>(3u);
    ExpectAtomicBadBounds(m_arena, m_model, IntegerBounds{ -10, 10, unknown });
}

TEST_F(FloatLifetimeTests, ReversedNonfiniteAndUnknownFloatBoundsAreAtomic){
    ExpectAtomicBadBounds(m_arena, m_model, FloatBounds{ 10.0, 1.0 });
    ExpectAtomicBadBounds(m_arena, m_model, FloatBounds{ Limit<f64>::s_QuietNaN, 10.0 });
    ExpectAtomicBadBounds(m_arena, m_model, FloatBounds{ -10.0, Limit<f64>::s_Infinity });
    const auto unknown = static_cast<NumericBoundsPolicy::Enum>(3u);
    ExpectAtomicBadBounds(m_arena, m_model, FloatBounds{ -10.0, 10.0, unknown });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

