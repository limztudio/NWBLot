// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <global/scope_exit.h>
#include <global/termination.h>

#include <logger/server/clipboard_text_internal.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_clipboard_text_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
namespace Clipboard = Log::FrameDetail;

TEST(LoggerClipboardText, EmptyInputOwnsOneTerminatedNativeString){
    Tests::TestArena<> testArena;
    Clipboard::MessageDeque messages(testArena.arena);
    auto memory = Clipboard::BuildClipboardText(messages);
    ASSERT_TRUE(memory);
    EXPECT_GE(GlobalSize(memory->native()), sizeof(tchar));
    const auto* const text = static_cast<const tchar*>(GlobalLock(memory->native()));
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text[0], tchar{});
    SetLastError(NO_ERROR);
    EXPECT_TRUE(GlobalUnlock(memory->native()) || GetLastError() == NO_ERROR);
}

TEST(LoggerClipboardText, MultipleUnicodeMessagesPreserveOrderCrLfAndTerminator){
    Tests::TestArena<> testArena;
    Clipboard::MessageDeque messages(testArena.arena);
    messages.emplace_back(Log::LogString(NWB_TEXT("first"), testArena.arena), Core::Common::LogType::Info);
    messages.emplace_back(Log::LogString(NWB_TEXT("\uD55C\uAE00 \u03A9"), testArena.arena), Core::Common::LogType::Warning);
    auto memory = Clipboard::BuildClipboardText(messages);
    ASSERT_TRUE(memory);
    constexpr TStringView s_Expected = NWB_TEXT("first\r\n\uD55C\uAE00 \u03A9\r\n");
    EXPECT_GE(GlobalSize(memory->native()), (s_Expected.size() + 1u) * sizeof(tchar));
    const auto* const text = static_cast<const tchar*>(GlobalLock(memory->native()));
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(TStringView(text, s_Expected.size()), s_Expected);
    EXPECT_EQ(text[s_Expected.size()], tchar{});
    SetLastError(NO_ERROR);
    EXPECT_TRUE(GlobalUnlock(memory->native()) || GetLastError() == NO_ERROR);
}

TEST(LoggerClipboardText, NativeOwnershipFreesUntransferredMemoryAndReleasesTransferredMemory){
    Tests::TestArena<> testArena;
    Clipboard::MessageDeque messages(testArena.arena);
    HGLOBAL freed = nullptr;
    {
        auto memory = Clipboard::BuildClipboardText(messages);
        ASSERT_TRUE(memory);
        freed = memory->native();
    }
    EXPECT_EQ(GlobalFlags(freed), GMEM_INVALID_HANDLE);
    HGLOBAL transferred = nullptr;
    {
        auto memory = Clipboard::BuildClipboardText(messages);
        ASSERT_TRUE(memory);
        transferred = memory->native();
        memory->release();
    }
    ScopeExit cleanup([&]()noexcept{
        if(transferred && GlobalFree(transferred))
            TerminateInvariant();
    }
    );
    EXPECT_NE(GlobalFlags(transferred), GMEM_INVALID_HANDLE);
    EXPECT_NE(GlobalSize(transferred), 0u);
}

TEST(LoggerClipboardText, ByteCountRejectsOverflowBeforeNativeAllocation){
    const auto empty = Clipboard::ClipboardByteCount(0u);
    ASSERT_TRUE(empty);
    EXPECT_EQ(*empty, sizeof(tchar));
    constexpr usize s_MaxCharacters = (Limit<usize>::s_Max / sizeof(tchar)) - 1u;
    const auto boundary = Clipboard::ClipboardByteCount(s_MaxCharacters);
    ASSERT_TRUE(boundary);
    EXPECT_EQ(*boundary, (s_MaxCharacters + 1u) * sizeof(tchar));
    const auto overflow = Clipboard::ClipboardByteCount(s_MaxCharacters + 1u);
    ASSERT_FALSE(overflow);
    EXPECT_EQ(overflow.error(), MakeErrorCode(ErrorCondition::value_too_large));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

