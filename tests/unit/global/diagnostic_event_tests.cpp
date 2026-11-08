// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/diagnostics.h>
#include <global/fixed_buffer.h>
#include <global/not_null.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_diagnostic_event_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_Unit = "unit";
static constexpr AStringView s_Value42 = "value 42";
static constexpr AStringView s_ValueFormatTrailing = "value {}.trailing";
static constexpr WStringView s_ValueFormatTrailingWide = L"value {}.trailing";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static u32 s_DiagnosticEventCaptureCount = 0u;
static AStringView s_DiagnosticEventName = {};
static AStringView s_DiagnosticEventCategory = {};
static AStringView s_DiagnosticEventExpression = {};
static AStringView s_DiagnosticEventMessage = {};
static AStringView s_DiagnosticEventFile = {};
static Array<char, s_MaxEventTextBytes> s_DiagnosticEventNameText = {};
static Array<char, s_MaxEventTextBytes> s_DiagnosticEventCategoryText = {};
static Array<char, s_MaxEventTextBytes> s_DiagnosticEventExpressionText = {};
static Array<char, s_MaxEventTextBytes> s_DiagnosticEventMessageText = {};
static Array<char, s_MaxEventTextBytes> s_DiagnosticEventFileText = {};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ResetDiagnosticEventCapture()noexcept{
    s_DiagnosticEventCaptureCount = 0u;
    s_DiagnosticEventName = {};
    s_DiagnosticEventCategory = {};
    s_DiagnosticEventExpression = {};
    s_DiagnosticEventMessage = {};
    s_DiagnosticEventFile = {};
    s_DiagnosticEventNameText[0u] = 0;
    s_DiagnosticEventCategoryText[0u] = 0;
    s_DiagnosticEventExpressionText[0u] = 0;
    s_DiagnosticEventMessageText[0u] = 0;
    s_DiagnosticEventFileText[0u] = 0;
}

static void RecordDiagnosticEvent(const DiagnosticEventRecord& record)noexcept{
    ++s_DiagnosticEventCaptureCount;
    CopyFixedBuffer(s_DiagnosticEventNameText, record.event);
    CopyFixedBuffer(s_DiagnosticEventCategoryText, record.category);
    CopyFixedBuffer(s_DiagnosticEventExpressionText, record.expression);
    CopyFixedBuffer(s_DiagnosticEventMessageText, record.message);
    CopyFixedBuffer(s_DiagnosticEventFileText, record.file);
    s_DiagnosticEventName = AStringView(s_DiagnosticEventNameText.data());
    s_DiagnosticEventCategory = AStringView(s_DiagnosticEventCategoryText.data());
    s_DiagnosticEventExpression = AStringView(s_DiagnosticEventExpressionText.data());
    s_DiagnosticEventMessage = AStringView(s_DiagnosticEventMessageText.data());
    s_DiagnosticEventFile = AStringView(s_DiagnosticEventFileText.data());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Global, DiagnosticFormatSupportsBoundedFormatViewsAndNullableAdapters){
    const DiagnosticEventText narrow = MakeDiagnosticEventText(s_ValueFormatTrailing.substr(0u, 8u), 42u);
    EXPECT_EQ(narrow.view(), s_Value42);
    const DiagnosticEventText wide = MakeDiagnosticEventText(s_ValueFormatTrailingWide.substr(0u, 8u), 42u);
    EXPECT_EQ(wide.view(), s_Value42);
    EXPECT_EQ(MakeDiagnosticEventText(static_cast<const char*>(nullptr), 42u).view(), AStringView{});
}

TEST(Global, DiagnosticEventTextViewsStopAtNullOrBufferBounds){
    DiagnosticEventText text;
    for(char& character : text.value)
        character = 'x';
    EXPECT_EQ(text.view().size(), sizeof(text.value));
    const DiagnosticEventText formatted = MakeDiagnosticEventText("{}", text);
    EXPECT_EQ(formatted.view().size(), sizeof(text.value) - 1u);
    EXPECT_EQ(formatted.view(), text.view().substr(0u, sizeof(text.value) - 1u));

    text.value[3u] = '\0';
    EXPECT_EQ(text.view(), AStringView("xxx"));
}

TEST(Global, DiagnosticEventHookRejectsReentryAndHonorsBoundedText){
    ResetDiagnosticEventCapture();

    const auto callback = MakeNotNull<DiagnosticEventCallback>([](const DiagnosticEventRecord& record)noexcept{
        RecordDiagnosticEvent(record);
        CaptureDiagnosticEvent("recursive", "ignored");
    });

    SetDiagnosticEventCallback(callback.get());
    CaptureDiagnosticEvent(AStringView("unit.trailing").substr(0u, 4u), AStringView("message.trailing").substr(0u, 7u), "diagnostics_test.cpp", 42u);
    ClearDiagnosticEventCallback(callback.get());
    CaptureDiagnosticEvent(s_Unit, "ignored");

    EXPECT_EQ(s_DiagnosticEventCaptureCount, 1u);
    ASSERT_NE(s_DiagnosticEventName.data(), nullptr);
    ASSERT_NE(s_DiagnosticEventCategory.data(), nullptr);
    ASSERT_NE(s_DiagnosticEventExpression.data(), nullptr);
    ASSERT_NE(s_DiagnosticEventMessage.data(), nullptr);
    ASSERT_NE(s_DiagnosticEventFile.data(), nullptr);
    EXPECT_EQ(s_DiagnosticEventName, AStringView(""));
    EXPECT_EQ(s_DiagnosticEventCategory, AStringView("unit"));
    EXPECT_EQ(s_DiagnosticEventExpression, AStringView(""));
    EXPECT_EQ(s_DiagnosticEventMessage, AStringView("message"));
    EXPECT_EQ(DiagnosticEventNameFromCategory("unknown"), StringView{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

