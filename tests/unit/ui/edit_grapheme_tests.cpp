// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_grapheme_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiEditTests;

struct GraphemeCase{
    AStringView text;
    AStringView expectedOffsets;

    template<usize TextSize, usize OffsetSize>
    constexpr GraphemeCase(const char (&textValue)[TextSize], const char (&offsetValue)[OffsetSize], const usize byteCount)
        : text(textValue, byteCount)
        , expectedOffsets(offsetValue, OffsetSize - 1u)
    {}
};

static constexpr GraphemeCase s_ConformanceCases[] = {
#include "edit_grapheme_cases_0.inc"
#include "edit_grapheme_cases_1.inc"
};

class UiEditGraphemeTests : public EditFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiEditGraphemeTests, PassesEveryPinnedOfficialUnicode17ExtendedGraphemeCase){
    static_assert(sizeof(s_ConformanceCases) / sizeof(GraphemeCase) == 766u);
    for(usize index = 0u; index < 766u; ++index){
        SCOPED_TRACE(index);
        const GraphemeCase& test = s_ConformanceCases[index];
        const auto actual = GraphemeSegmentation::Build(m_arena, test.text);
        ASSERT_TRUE(actual);
        const AStringView expectedOffsets = test.expectedOffsets;
        usize cursor = 0u;
        usize offsetIndex = 0u;
        while(cursor < expectedOffsets.size()){
            usize expected = 0u;
            while(cursor < expectedOffsets.size() && expectedOffsets[cursor] >= '0' && expectedOffsets[cursor] <= '9'){
                expected = expected * 10u + static_cast<usize>(expectedOffsets[cursor] - '0');
                ++cursor;
            }
            ASSERT_LT(offsetIndex, actual->size());
            EXPECT_EQ((*actual)[offsetIndex], expected);
            ++offsetIndex;
            if(cursor < expectedOffsets.size() && expectedOffsets[cursor] == ',')
                ++cursor;
        }
        EXPECT_EQ(offsetIndex, actual->size());
    }
}

TEST_F(UiEditGraphemeTests, RegionalIndicatorsPairAndEmojiModifierZwjSequencesRemainWhole){
    const AStringView flags("\xF0\x9F\x87\xB0\xF0\x9F\x87\xB7\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8"
        "\xF0\x9F\x87\xAF");
    auto boundaries = GraphemeSegmentation::Build(m_arena, flags);
    ASSERT_TRUE(boundaries);
    ASSERT_EQ(boundaries->size(), 4u);
    EXPECT_EQ((*boundaries)[0], 0u);
    EXPECT_EQ((*boundaries)[1], 8u);
    EXPECT_EQ((*boundaries)[2], 16u);
    EXPECT_EQ((*boundaries)[3], 20u);
    const AStringView emoji("\xF0\x9F\x91\xA9\xF0\x9F\x8F\xBD\xE2\x80\x8D\xF0\x9F\x92\xBB");
    boundaries = GraphemeSegmentation::Build(m_arena, emoji);
    ASSERT_TRUE(boundaries);
    ASSERT_EQ(boundaries->size(), 2u);
    EXPECT_EQ((*boundaries)[1], 15u);
    ASSERT_TRUE(m_model.setText(emoji));
    ASSERT_TRUE(m_model.backspace());
    EXPECT_TRUE(m_model.text().empty());
}

TEST_F(UiEditGraphemeTests, GeneralSegmentationAcceptsCrLfButSingleLineValidationRejectsIt){
    const auto boundaries = GraphemeSegmentation::Build(m_arena, "x\r\ny");
    ASSERT_TRUE(boundaries);
    ASSERT_EQ(boundaries->size(), 4u);
    EXPECT_EQ((*boundaries)[0], 0u);
    EXPECT_EQ((*boundaries)[1], 1u);
    EXPECT_EQ((*boundaries)[2], 3u);
    EXPECT_EQ((*boundaries)[3], 4u);
    EXPECT_FALSE(GraphemeSegmentation::Build(m_arena, "x\r\ny", true));
    EXPECT_EQ((*boundaries)[2], 3u);
    EXPECT_TRUE(GraphemeSegmentation::Validate(AStringView("\0", 1u)));
    EXPECT_FALSE(GraphemeSegmentation::Validate(AStringView("\0", 1u), true));
}

TEST_F(UiEditGraphemeTests, InvalidUtf8FailsBeforeAllocatingBoundaries){
    const auto boundaries = GraphemeSegmentation::Build(m_arena, "ok");
    ASSERT_TRUE(boundaries);
    const auto before = m_arena.memoryStats();
    EXPECT_FALSE(GraphemeSegmentation::Build(m_arena, "\xED\xA0\x80"));
    EXPECT_FALSE(GraphemeSegmentation::Build(m_arena, "\xF5\x80\x80\x80"));
    EXPECT_FALSE(GraphemeSegmentation::Build(m_arena, "\x80"));
    EXPECT_EQ(m_arena.memoryStats().allocationCount, before.allocationCount);
    ASSERT_EQ(boundaries->size(), 3u);
    EXPECT_EQ((*boundaries)[0], 0u);
    EXPECT_EQ((*boundaries)[1], 1u);
    EXPECT_EQ((*boundaries)[2], 2u);
}

TEST_F(UiEditGraphemeTests, ScalarPositionsDifferFromGraphemePositionsWithoutSplittingUtf8){
    const AStringView text("a\xCC\x81");
    ASSERT_TRUE(GraphemeSegmentation::Validate(text));
    EXPECT_TRUE(GraphemeSegmentation::IsScalarBoundary(text, 0u));
    EXPECT_TRUE(GraphemeSegmentation::IsScalarBoundary(text, 1u));
    EXPECT_FALSE(GraphemeSegmentation::IsScalarBoundary(text, 2u));
    EXPECT_TRUE(GraphemeSegmentation::IsScalarBoundary(text, 3u));
    EXPECT_FALSE(GraphemeSegmentation::IsScalarBoundary(text, 4u));
    EXPECT_TRUE(GraphemeSegmentation::IsScalarBoundary({}, 0u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

