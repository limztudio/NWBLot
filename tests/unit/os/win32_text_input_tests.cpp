// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/os/win32/text_input.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <windows.h>
#include <imm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_win32_text_input_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;
inline constexpr WStringView s_ClassName = L"NWBTextInputUnitTest";

class Win32TextInputFixture : public testing::Test{
public:
    static LRESULT CALLBACK WindowProc(const HWND window, const UINT message, const WPARAM wParam, const LPARAM lParam){
        if(message == WM_NCCREATE){
            const auto* const creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            SetLastError(ERROR_SUCCESS);
            const LONG_PTR previous = SetWindowLongPtrW(
                window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams)
            );
            if(previous == 0 && GetLastError() != ERROR_SUCCESS)
                return FALSE;
        }
        auto* const fixture = reinterpret_cast<Win32TextInputFixture*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if(fixture && fixture->m_service){
            isize forwardedLParam = lParam;
            if(ResolveWin32TextInputContextMessage(*fixture->m_service, message, wParam, lParam, forwardedLParam)){
                ++fixture->m_contextForwardCount;
                fixture->m_lastContextForwardedFlags = forwardedLParam;
                if(fixture->m_cancelOnNextContextForward){
                    fixture->m_cancelOnNextContextForward = false;
                    fixture->m_cancelledDuringContextToken = fixture->m_service->activeSession();
                    fixture->m_contextFocusLossSucceeded = fixture->m_service->setFocused(false);
                    fixture->m_contextFocusRestoreSucceeded = fixture->m_service->setFocused(true);
                }
                fixture->m_lastContextDefaultResult = DefWindowProcW(window, message, wParam, static_cast<LPARAM>(forwardedLParam));
                return fixture->m_lastContextDefaultResult;
            }
            if(message == WM_UNICHAR && wParam == UNICODE_NOCHAR)
                return TRUE;
            if(DispatchWin32TextInputMessage(*fixture->m_service, message, wParam, lParam))
                return 0;
            if(message == WM_CHAR){
                u32 codePoint = 0u;
                if(DecodeWin32FallbackCharInput(*fixture->m_service, static_cast<u32>(wParam), codePoint)){
                    ++fixture->m_sceneCharacterCount;
                    fixture->m_sceneCodePoint = codePoint;
                }
                return 0;
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    Win32TextInputFixture()
        : m_loggerGuard(m_logger, Common::LoggerBreakPolicy::ReportOnly)
    {}


protected:
    virtual void SetUp()override{
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW nativeClass = {};
        nativeClass.lpfnWndProc = WindowProc;
        nativeClass.hInstance = instance;
        nativeClass.lpszClassName = s_ClassName.data();
        const ATOM registered = RegisterClassW(&nativeClass);
        ASSERT_TRUE(registered != 0u || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
        m_window = CreateWindowExW(0u, s_ClassName.data(), L"", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, instance, this);
        ASSERT_NE(m_window, nullptr);
        m_service = CreateTextInputService(m_arena.arena, m_window);
        ASSERT_NE(m_service.get(), nullptr);
        ASSERT_TRUE(m_service->setFocused(true));
        const TextInputSessionDesc desc{ { 10, 15, 2, 18 }, {}, 0u, 0u };
        const TextInputBeginResult begun = m_service->begin(desc);
        ASSERT_EQ(begun.admission, TextInputAdmission::Accepted);
        m_token = begun.token;
        ASSERT_TRUE(m_token.valid());
    }

    virtual void TearDown()override{
        m_service.reset();
        if(m_window){
            EXPECT_NE(DestroyWindow(m_window), FALSE);
            m_window = nullptr;
        }
        EXPECT_EQ(m_logger.errorCount(), 0u);
        EXPECT_NE(m_logger.lastType(), Common::LogType::Fatal);
    }


protected:
    NWB::Tests::CapturingLogger m_logger;
    Common::LoggerRegistrationGuard m_loggerGuard;
    NWB::Tests::TestArena<> m_arena;
    GlobalUniquePtr<ITextInputService> m_service;
    HWND m_window = nullptr;
    TextInputSessionToken m_token;
    usize m_sceneCharacterCount = 0u;
    u32 m_sceneCodePoint = 0u;
    usize m_contextForwardCount = 0u;
    isize m_lastContextForwardedFlags = 0;
    LRESULT m_lastContextDefaultResult = 0;
    TextInputSessionToken m_cancelledDuringContextToken;
    bool m_cancelOnNextContextForward = false;
    bool m_contextFocusLossSucceeded = false;
    bool m_contextFocusRestoreSucceeded = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(Win32TextInputFixture, ActiveCharAndUnicodeMessagesProduceOwnedUtf8Once){
    EXPECT_EQ(m_service->capabilities().backend, TextInputBackend::Win32Imm32);
    EXPECT_TRUE(m_service->capabilities().commit);
    EXPECT_FALSE(m_service->capabilities().surrounding);
    EXPECT_FALSE(m_service->capabilities().deleteSurrounding);
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'A', 0), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xd55cu, 0), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_UNICHAR, 0x1f600u, 0), 0);
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "A");
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "\xED\x95\x9C");
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "\xF0\x9F\x98\x80");
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}

TEST_F(Win32TextInputFixture, SurrogatePairRemainsPerServiceUntilComplete){
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xd83du, 0), 0);
    TextInputEvent event(m_arena.arena);
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xde00u, 0), 0);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "\xF0\x9F\x98\x80");
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}

TEST_F(Win32TextInputFixture, AggregateCharacterCountsIgnoreHighFlagsAndKeepScalarBoundaries){
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'A', 0x40000003), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_SYSCHAR, 0xd55cu, 0x20000002), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_UNICHAR, 0x1f600u, 2), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xd83du, 2), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xde00u, 2), 0);
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "AAA");
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "\xED\x95\x9C\xED\x95\x9C");
    for(usize index = 0u; index < 2u; ++index){
        ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
        EXPECT_EQ(event.text, "\xF0\x9F\x98\x80\xF0\x9F\x98\x80");
    }
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}

TEST_F(Win32TextInputFixture, MaximumAggregateSupplementaryCommitBatchesWithoutEventOverflow){
    EXPECT_EQ(SendMessageW(m_window, WM_UNICHAR, 0x1f600u, 0xffff), 0);
    ASSERT_EQ(m_service->activeSession(), m_token);
    TextInputEvent event(m_arena.arena);
    usize totalBytes = 0u;
    u64 sequence = 0u;
    while(totalBytes < 0xffffu * 4u){
        ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
        ASSERT_EQ(event.kind, TextInputEventKind::Commit);
        ASSERT_FALSE(event.text.empty());
        EXPECT_LE(event.text.size(), s_TextInputMaxEventTextBytes);
        ASSERT_EQ(event.text.size() % 4u, 0u);
        EXPECT_GT(event.sequence, sequence);
        sequence = event.sequence;
        for(usize offset = 0u; offset < event.text.size(); offset += 4u)
            ASSERT_EQ(AStringView(event.text.data() + offset, 4u), "\xF0\x9F\x98\x80");
        totalBytes += event.text.size();
    }
    EXPECT_EQ(totalBytes, 0xffffu * 4u);
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}

TEST_F(Win32TextInputFixture, AggregateCommitOverflowDiscardsAlreadyQueuedPrefix){
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'x', 5), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_UNICHAR, 0x1f600u, 0xffff), 0);
    EXPECT_FALSE(m_service->activeSession().valid());
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Cancelled);
    EXPECT_EQ(event.cancelReason, TextInputCancelReason::Overflow);
    EXPECT_TRUE(event.text.empty());
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::InvalidSession);
}

TEST_F(Win32TextInputFixture, IsolatedLowSurrogateCancelsWithExplicitFailure){
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xde00u, 0), 0);
    EXPECT_FALSE(m_service->activeSession().valid());
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Cancelled);
    EXPECT_EQ(event.cancelReason, TextInputCancelReason::NativeFailure);
    EXPECT_TRUE(event.text.empty());
    EXPECT_TRUE(m_logger.sawMessageContaining(NWB_TEXT("Text input: native delivery rejected")));
    EXPECT_EQ(m_logger.lastType(), Common::LogType::Warning);
}

TEST_F(Win32TextInputFixture, FocusLossClearsPendingSurrogateBeforeReplacement){
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xd83du, 0), 0);
    ASSERT_TRUE(m_service->setFocused(false));
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.cancelReason, TextInputCancelReason::FocusLost);
    EXPECT_FALSE(DispatchWin32TextInputMessage(*m_service, WM_CHAR, 'x', 0));
    ASSERT_TRUE(m_service->setFocused(true));
    m_token = m_service->begin({}).token;
    ASSERT_TRUE(m_token.valid());
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'B', 0), 0);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "B");
}

TEST_F(Win32TextInputFixture, EndDiscardAndReplacementCannotReusePartialUtf16){
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xd83du, 0), 0);
    ASSERT_TRUE(m_service->end(m_token));
    m_token = m_service->begin({}).token;
    ASSERT_TRUE(m_token.valid());
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xde00u, 0), 0);
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Cancelled);
}

TEST_F(Win32TextInputFixture, ControlCharactersStayOutOfCommittedDocumentText){
    for(const WPARAM character : { 0x08u, 0x09u, 0x0du, 0x1bu, 0x7fu })
        EXPECT_EQ(SendMessageW(m_window, WM_CHAR, character, 0), 0);
    TextInputEvent event(m_arena.arena);
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
    EXPECT_EQ(SendMessageW(m_window, WM_UNICHAR, UNICODE_NOCHAR, 0), TRUE);
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}

TEST_F(Win32TextInputFixture, ImeCharIsConsumedWithoutDuplicateCommitAndFollowingTypingSurvives){
    EXPECT_EQ(SendMessageW(m_window, WM_IME_STARTCOMPOSITION, 0u, 0), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_CHAR, 0xd55cu, 0), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_ENDCOMPOSITION, 0u, 0), 0);
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Preedit);
    EXPECT_TRUE(event.text.empty());
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xd55cu, 0), 0);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Commit);
    EXPECT_EQ(event.text, "\xED\x95\x9C");
}

TEST_F(Win32TextInputFixture, CompositionWithoutCurrentStartCannotDeliverAcrossSessionBoundary){
    EXPECT_EQ(SendMessageW(m_window, WM_IME_STARTCOMPOSITION, 0u, 0), 0);
    ASSERT_TRUE(m_service->end(m_token));
    m_token = m_service->begin({}).token;
    ASSERT_TRUE(m_token.valid());
    EXPECT_EQ(SendMessageW(m_window, WM_IME_COMPOSITION, 0u, GCS_RESULTSTR), 0);
    TextInputEvent event(m_arena.arena);
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}

TEST_F(Win32TextInputFixture, CandidateAndCompositionCaretUseClientPixels){
    const HIMC context = ImmGetContext(m_window);
    if(!context)
        GTEST_SKIP() << "IMM32 input context is unavailable; direct Unicode commits remain supported";
    COMPOSITIONFORM composition = {};
    CANDIDATEFORM candidate = {};
    const bool compositionRead = ImmGetCompositionWindow(context, &composition) != FALSE;
    const bool candidateRead = ImmGetCandidateWindow(context, 0u, &candidate) != FALSE;
    EXPECT_NE(ImmReleaseContext(m_window, context), FALSE);
    ASSERT_TRUE(compositionRead);
    ASSERT_TRUE(candidateRead);
    EXPECT_EQ(composition.dwStyle, CFS_POINT);
    EXPECT_EQ(composition.ptCurrentPos.x, 10);
    EXPECT_EQ(composition.ptCurrentPos.y, 15);
    EXPECT_EQ(candidate.dwStyle, CFS_EXCLUDE);
    EXPECT_EQ(candidate.ptCurrentPos.y, 33);
    EXPECT_EQ(candidate.rcArea.right, 12);
    EXPECT_EQ(candidate.rcArea.bottom, 33);
    ASSERT_EQ(m_service->updateCaret(m_token, { 50, 60, 3, 24 }), TextInputAdmission::Accepted);
    const HIMC updated = ImmGetContext(m_window);
    ASSERT_NE(updated, nullptr);
    const bool updatedRead = ImmGetCandidateWindow(updated, 0u, &candidate) != FALSE;
    EXPECT_NE(ImmReleaseContext(m_window, updated), FALSE);
    ASSERT_TRUE(updatedRead);
    EXPECT_EQ(candidate.ptCurrentPos.x, 50);
    EXPECT_EQ(candidate.ptCurrentPos.y, 84);
    EXPECT_EQ(candidate.rcArea.right, 53);
}

TEST_F(Win32TextInputFixture, BridgeRejectsUnsupportedForeignServiceBeforeDowncast){
    const GlobalUniquePtr<ITextInputService> unsupported = CreateTextInputService(m_arena.arena, nullptr);
    EXPECT_FALSE(DispatchWin32TextInputMessage(*unsupported, WM_CHAR, 'x', 0));
    EXPECT_FALSE(DispatchWin32TextInputMessage(*m_service, WM_MOUSEMOVE, 0u, 0));
    isize forwardedLParam = 123;
    EXPECT_FALSE(ResolveWin32TextInputContextMessage(*unsupported, WM_IME_SETCONTEXT, TRUE, 456, forwardedLParam));
    EXPECT_EQ(forwardedLParam, 123);
    EXPECT_FALSE(ResolveWin32TextInputContextMessage(*m_service, WM_MOUSEMOVE, 0u, 456, forwardedLParam));
    EXPECT_EQ(forwardedLParam, 123);
    TextInputEvent event(m_arena.arena);
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}

TEST_F(Win32TextInputFixture, ImeContextVisibilityTracksSessionAfterActivationAndPreservesOtherFlags){
    ASSERT_TRUE(m_service->end(m_token));
    const isize flags = static_cast<isize>(ISC_SHOWUICOMPOSITIONWINDOW | ISC_SHOWUICANDIDATEWINDOW
        | (ISC_SHOWUICANDIDATEWINDOW << 2u) | (1u << 20u));
    const usize before = m_contextForwardCount;
    const LRESULT inactiveResult = SendMessageW(m_window, WM_IME_SETCONTEXT, TRUE, static_cast<LPARAM>(flags));
    EXPECT_EQ(m_contextForwardCount, before + 1u);
    EXPECT_EQ(m_lastContextForwardedFlags, flags);
    EXPECT_EQ(inactiveResult, m_lastContextDefaultResult);

    const TextInputBeginResult begun = m_service->begin({});
    ASSERT_EQ(begun.admission, TextInputAdmission::Accepted);
    m_token = begun.token;
    EXPECT_EQ(m_contextForwardCount, before + 2u);
    EXPECT_EQ(m_lastContextForwardedFlags, flags & ~static_cast<isize>(ISC_SHOWUICOMPOSITIONWINDOW));
    const LRESULT activeResult = SendMessageW(m_window, WM_IME_SETCONTEXT, TRUE, static_cast<LPARAM>(flags));
    EXPECT_EQ(m_contextForwardCount, before + 3u);
    EXPECT_EQ(m_lastContextForwardedFlags, flags & ~static_cast<isize>(ISC_SHOWUICOMPOSITIONWINDOW));
    EXPECT_EQ(activeResult, m_lastContextDefaultResult);
    ASSERT_TRUE(m_service->end(m_token));
    EXPECT_EQ(m_contextForwardCount, before + 4u);
    EXPECT_EQ(m_lastContextForwardedFlags, flags);

    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'Z', 0), 0);
    EXPECT_EQ(m_sceneCharacterCount, 1u);
    EXPECT_EQ(m_sceneCodePoint, static_cast<u32>('Z'));
}

TEST_F(Win32TextInputFixture, ImeContextReplayNeverReactivatesAfterFocusLossOrNativeDeactivation){
    ASSERT_TRUE(m_service->end(m_token));
    const isize flags = static_cast<isize>(ISC_SHOWUICOMPOSITIONWINDOW | ISC_SHOWUICANDIDATEWINDOW | (1u << 20u));
    SendMessageW(m_window, WM_IME_SETCONTEXT, TRUE, static_cast<LPARAM>(flags));
    const TextInputBeginResult begun = m_service->begin({});
    ASSERT_EQ(begun.admission, TextInputAdmission::Accepted);
    m_token = begun.token;
    const usize beforeFocusLoss = m_contextForwardCount;
    ASSERT_TRUE(m_service->setFocused(false));
    EXPECT_EQ(m_contextForwardCount, beforeFocusLoss);
    const LRESULT deactivationResult = SendMessageW(m_window, WM_IME_SETCONTEXT, FALSE, static_cast<LPARAM>(flags));
    EXPECT_EQ(m_contextForwardCount, beforeFocusLoss + 1u);
    EXPECT_EQ(m_lastContextForwardedFlags, flags);
    EXPECT_EQ(deactivationResult, m_lastContextDefaultResult);

    ASSERT_TRUE(m_service->setFocused(true));
    const TextInputBeginResult second = m_service->begin({});
    ASSERT_EQ(second.admission, TextInputAdmission::Accepted);
    m_token = second.token;
    EXPECT_EQ(m_contextForwardCount, beforeFocusLoss + 1u);
    ASSERT_TRUE(m_service->end(m_token));
    EXPECT_EQ(m_contextForwardCount, beforeFocusLoss + 1u);
}

TEST_F(Win32TextInputFixture, ReentrantFocusCancellationRestoresNativeImeVisibilityAndKeepsCancelEvent){
    ASSERT_TRUE(m_service->end(m_token));
    const isize flags = static_cast<isize>(ISC_SHOWUICOMPOSITIONWINDOW | ISC_SHOWUICANDIDATEWINDOW | (1u << 20u));
    SendMessageW(m_window, WM_IME_SETCONTEXT, TRUE, static_cast<LPARAM>(flags));
    const usize before = m_contextForwardCount;
    m_cancelOnNextContextForward = true;
    const TextInputBeginResult begun = m_service->begin({});
    EXPECT_EQ(begun.admission, TextInputAdmission::Unavailable);
    EXPECT_FALSE(begun.token.valid());
    EXPECT_FALSE(m_cancelOnNextContextForward);
    EXPECT_TRUE(m_contextFocusLossSucceeded);
    EXPECT_TRUE(m_contextFocusRestoreSucceeded);
    EXPECT_TRUE(m_cancelledDuringContextToken.valid());
    EXPECT_EQ(m_contextForwardCount, before + 2u);
    EXPECT_EQ(m_lastContextForwardedFlags, flags);

    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_cancelledDuringContextToken, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Cancelled);
    EXPECT_EQ(event.cancelReason, TextInputCancelReason::FocusLost);
    EXPECT_EQ(m_service->poll(m_cancelledDuringContextToken, event), TextInputPollResult::InvalidSession);
    EXPECT_EQ(m_sceneCharacterCount, 0u);
}

TEST_F(Win32TextInputFixture, SceneFallbackCannotJoinSurrogatesAcrossSessionOwnership){
    ASSERT_TRUE(m_service->end(m_token));
    u32 codePoint = 123u;
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xd83du, codePoint));
    EXPECT_EQ(codePoint, 0u);
    m_token = m_service->begin({}).token;
    ASSERT_TRUE(m_token.valid());
    ASSERT_TRUE(m_service->end(m_token));
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xde00u, codePoint));
    EXPECT_EQ(codePoint, 0u);
    EXPECT_TRUE(DecodeWin32FallbackCharInput(*m_service, 'A', codePoint));
    EXPECT_EQ(codePoint, static_cast<u32>('A'));
}

TEST_F(Win32TextInputFixture, SceneFallbackFocusResetDiscardsPartialPair){
    ASSERT_TRUE(m_service->end(m_token));
    u32 codePoint = 0u;
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xd83du, codePoint));
    ASSERT_TRUE(m_service->setFocused(false));
    ASSERT_TRUE(ResetWin32FallbackCharInput(*m_service));
    ASSERT_TRUE(m_service->setFocused(true));
    ASSERT_TRUE(ResetWin32FallbackCharInput(*m_service));
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xde00u, codePoint));
    EXPECT_EQ(codePoint, 0u);
}

TEST_F(Win32TextInputFixture, SceneFallbackDecoderNeverConsumesActivePair){
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xd83du, 0), 0);
    u32 codePoint = 123u;
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xde00u, codePoint));
    EXPECT_EQ(codePoint, 0u);
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xde00u, 0), 0);
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.text, "\xF0\x9F\x98\x80");
}

TEST_F(Win32TextInputFixture, SceneFallbackSurrogateStateIsPerServiceAndValidatesUnits){
    ASSERT_TRUE(m_service->end(m_token));
    const GlobalUniquePtr<ITextInputService> second = CreateTextInputService(m_arena.arena, m_window);
    ASSERT_NE(second.get(), nullptr);
    ASSERT_TRUE(second->setFocused(true));
    u32 codePoint = 0u;
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xd83du, codePoint));
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*second, 0xde00u, codePoint));
    EXPECT_TRUE(DecodeWin32FallbackCharInput(*m_service, 0xde00u, codePoint));
    EXPECT_EQ(codePoint, 0x1f600u);
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0x110000u, codePoint));
    EXPECT_EQ(codePoint, 0u);
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0u, codePoint));
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xd83du, codePoint));
    EXPECT_TRUE(DecodeWin32FallbackCharInput(*m_service, 'B', codePoint));
    EXPECT_EQ(codePoint, static_cast<u32>('B'));
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xde00u, codePoint));
}

TEST_F(Win32TextInputFixture, SceneFallbackBridgeRejectsForeignServiceAndWrongThread){
    ASSERT_TRUE(m_service->end(m_token));
    const GlobalUniquePtr<ITextInputService> unsupported = CreateTextInputService(m_arena.arena, nullptr);
    u32 codePoint = 123u;
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*unsupported, 'A', codePoint));
    EXPECT_EQ(codePoint, 0u);
    EXPECT_FALSE(ResetWin32FallbackCharInput(*unsupported));
    Thread worker([&](){
        u32 workerCodePoint = 123u;
        EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 'A', workerCodePoint));
        EXPECT_EQ(workerCodePoint, 0u);
        EXPECT_FALSE(ResetWin32FallbackCharInput(*m_service));
    });
    worker.join();
    EXPECT_TRUE(DecodeWin32FallbackCharInput(*m_service, 'C', codePoint));
    EXPECT_EQ(codePoint, static_cast<u32>('C'));
}


TEST_F(Win32TextInputFixture, InsertCharCreatesProvisionalPreeditWithoutCommitAndHonorsNoMoveCaret){
    EXPECT_EQ(SendMessageW(m_window, WM_IME_STARTCOMPOSITION, 0u, 0), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_COMPOSITION, 0xd55cu, CS_INSERTCHAR | CS_NOMOVECARET), 0);
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Preedit);
    EXPECT_EQ(event.text, "\xED\x95\x9C");
    EXPECT_EQ(event.anchorByte, 0u);
    EXPECT_EQ(event.caretByte, 0u);
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_COMPOSITION, 'A', CS_INSERTCHAR), 0);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Preedit);
    EXPECT_EQ(event.text, "A\xED\x95\x9C");
    EXPECT_EQ(event.caretByte, 1u);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_ENDCOMPOSITION, 0u, 0), 0);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Preedit);
    EXPECT_TRUE(event.text.empty());
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}

TEST_F(Win32TextInputFixture, InsertCharSupplementaryPairProducesOneUtf8PreeditAndCancellationClearsIt){
    EXPECT_EQ(SendMessageW(m_window, WM_IME_STARTCOMPOSITION, 0u, 0), 0);
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_COMPOSITION, 0xd83du, CS_INSERTCHAR), 0);
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_COMPOSITION, 0xde00u, CS_INSERTCHAR), 0);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Preedit);
    EXPECT_EQ(event.text, "\xF0\x9F\x98\x80");
    EXPECT_EQ(event.caretByte, 4u);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_COMPOSITION, 0u, 0), 0);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Preedit);
    EXPECT_TRUE(event.text.empty());
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::Pending);
}


TEST_F(Win32TextInputFixture, KeyboardFocusMessagesCancelQueuedWorkWithoutActivationChange){
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'A', 0), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_STARTCOMPOSITION, 0u, 0), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_IME_COMPOSITION, 0xd55cu, CS_INSERTCHAR), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_KILLFOCUS, 0u, 0), 0);
    EXPECT_FALSE(m_service->activeSession().valid());
    TextInputEvent event(m_arena.arena);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Cancelled);
    EXPECT_EQ(event.cancelReason, TextInputCancelReason::FocusLost);
    EXPECT_TRUE(event.text.empty());
    EXPECT_EQ(m_service->poll(m_token, event), TextInputPollResult::InvalidSession);
    EXPECT_EQ(m_service->begin({}).admission, TextInputAdmission::Unavailable);
    EXPECT_EQ(SendMessageW(m_window, WM_SETFOCUS, 0u, 0), 0);
    m_token = m_service->begin({}).token;
    ASSERT_TRUE(m_token.valid());
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'B', 0), 0);
    ASSERT_EQ(m_service->poll(m_token, event), TextInputPollResult::Event);
    EXPECT_EQ(event.kind, TextInputEventKind::Commit);
    EXPECT_EQ(event.text, "B");
}

TEST_F(Win32TextInputFixture, InactiveKeyboardFocusMessagesResetSceneSurrogateState){
    ASSERT_TRUE(m_service->end(m_token));
    u32 codePoint = 0u;
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xd83du, codePoint));
    EXPECT_EQ(SendMessageW(m_window, WM_KILLFOCUS, 0u, 0), 0);
    EXPECT_EQ(SendMessageW(m_window, WM_SETFOCUS, 0u, 0), 0);
    EXPECT_FALSE(DecodeWin32FallbackCharInput(*m_service, 0xde00u, codePoint));
    EXPECT_EQ(codePoint, 0u);
}


TEST_F(Win32TextInputFixture, UnfocusedSceneFallbackRefusesLateWindowCharacterAndClearsPartialPair){
    ASSERT_TRUE(m_service->end(m_token));
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xd83du, 0), 0);
    EXPECT_EQ(m_sceneCharacterCount, 0u);
    // Direct focus state update leaves fallback reset to the refusal guard, as a defensive late-message case.
    ASSERT_TRUE(m_service->setFocused(false));
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'A', 0), 0);
    EXPECT_EQ(m_sceneCharacterCount, 0u);
    ASSERT_TRUE(m_service->setFocused(true));
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 0xde00u, 0), 0);
    EXPECT_EQ(m_sceneCharacterCount, 0u);
    EXPECT_EQ(SendMessageW(m_window, WM_CHAR, 'B', 0), 0);
    EXPECT_EQ(m_sceneCharacterCount, 1u);
    EXPECT_EQ(m_sceneCodePoint, static_cast<u32>('B'));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

