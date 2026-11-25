// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/os/text_input_service.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Win32TextInputService final : public QueuedTextInputService{
public:
    Win32TextInputService(Alloc::GlobalArena& arena, NotNull<void*> nativeWindowHandle);
    virtual ~Win32TextInputService()noexcept override;


public:
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override;
    [[nodiscard]] b8 handleMessage(u32 message, usize wParam, isize lParam);
    [[nodiscard]] Expected<isize> resolveContextMessage(u32 message, usize wParam, isize lParam)noexcept;
    [[nodiscard]] Expected<u32> decodeFallbackCharInput(u32 unit)noexcept;
    [[nodiscard]] b8 resetFallbackCharInput()noexcept;


protected:
    [[nodiscard]] virtual TextInputAdmission::Enum startNativeSession(TextInputSessionToken token, const TextInputSessionDesc& desc)override;
    virtual void endNativeSession(TextInputSessionToken token)override;
    [[nodiscard]] virtual TextInputAdmission::Enum updateNativeCaret(TextInputRect caret)override;


private:
    [[nodiscard]] TextInputAdmission::Enum acceptCodePoint(TextInputSessionToken token, u32 codePoint, u32 repeatCount);
    [[nodiscard]] TextInputAdmission::Enum acceptUtf16Unit(TextInputSessionToken token, u32 unit, u32 repeatCount);
    [[nodiscard]] TextInputAdmission::Enum readCompositionText(NotNull<void*> nativeContext, u32 index);
    [[nodiscard]] Expected<usize, TextInputAdmission::Enum> compositionCursor(NotNull<void*> nativeContext)noexcept;
    void readCompositionClauses(NotNull<void*> nativeContext, AStringView utf8Text);
    [[nodiscard]] TextInputAdmission::Enum acceptComposition(TextInputSessionToken token, usize wParam, isize flags);
    [[nodiscard]] TextInputAdmission::Enum acceptInsertedPreedit(TextInputSessionToken token, u32 unit, b8 moveCaret);
    [[nodiscard]] TextInputAdmission::Enum publishCompositionPreedit(TextInputSessionToken token, AStringView text, usize anchorByte, usize caretByte, Span<const TextInputPreeditClause> clauses = {});
    void clearCompositionPreedit()noexcept;
    void rejectNativeInput(TextInputSessionToken token, TextInputAdmission::Enum admission);
    void replayContextVisibility();


private:
    NotNull<void*> m_nativeWindowHandle;
    WString<Alloc::GlobalArena> m_wideText;
    AString<Alloc::GlobalArena> m_utf8Text;
    AString<Alloc::GlobalArena> m_preeditText;
    Vector<u8, Alloc::GlobalArena> m_compAttr;
    Vector<u32, Alloc::GlobalArena> m_compClause;
    Vector<usize, Alloc::GlobalArena> m_utf16ByteOffsets;
    Vector<TextInputPreeditClause, Alloc::GlobalArena> m_preeditClauses;
    TextInputSessionToken m_compositionToken;
    u32 m_pendingHighSurrogate = 0u;
    u32 m_pendingPreeditHighSurrogate = 0u;
    usize m_preeditCaretByte = 0u;
    isize m_nativeContextFlags = 0;
    b8 m_pendingPreeditMoveCaret = true;
    b8 m_imeAvailable = false;
    b8 m_nativeContextKnown = false;
    b8 m_nativeContextActive = false;
    b8 m_replayingContext = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
