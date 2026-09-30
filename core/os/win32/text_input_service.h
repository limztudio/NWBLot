// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/os/text_input_service.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// IMM32 compatibility backend; this does not implement TSF reconversion or surrounding-text deletion.
class Win32TextInputService final : public QueuedTextInputService{
public:
    Win32TextInputService(Alloc::GlobalArena& arena, NotNull<void*> nativeWindowHandle);
    virtual ~Win32TextInputService()override;


public:
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override;
    [[nodiscard]] bool handleMessage(u32 message, usize wParam, isize lParam);
    [[nodiscard]] bool decodeFallbackCharInput(u32 unit, u32& codePoint);
    [[nodiscard]] bool resetFallbackCharInput();


protected:
    [[nodiscard]] virtual TextInputAdmission::Enum startNativeSession(
        TextInputSessionToken token, const TextInputSessionDesc& desc
    )override;
    virtual void endNativeSession(TextInputSessionToken token)override;
    [[nodiscard]] virtual TextInputAdmission::Enum updateNativeCaret(TextInputRect caret)override;


private:
    [[nodiscard]] TextInputAdmission::Enum acceptCodePoint(TextInputSessionToken token, u32 codePoint, u32 repeatCount);
    [[nodiscard]] TextInputAdmission::Enum acceptUtf16Unit(TextInputSessionToken token, u32 unit, u32 repeatCount);
    [[nodiscard]] TextInputAdmission::Enum readCompositionText(NotNull<void*> nativeContext, u32 index);
    [[nodiscard]] TextInputAdmission::Enum compositionCursor(NotNull<void*> nativeContext, usize& byteOffset);
    [[nodiscard]] TextInputAdmission::Enum acceptComposition(TextInputSessionToken token, usize wParam, isize flags);
    [[nodiscard]] TextInputAdmission::Enum acceptInsertedPreedit(TextInputSessionToken token, u32 unit, bool moveCaret);
    [[nodiscard]] TextInputAdmission::Enum publishCompositionPreedit(
        TextInputSessionToken token, AStringView text, usize anchorByte, usize caretByte
    );
    void clearCompositionPreedit();
    void rejectNativeInput(TextInputSessionToken token, TextInputAdmission::Enum admission);


private:
    NotNull<void*> m_nativeWindowHandle;
    WString<Alloc::GlobalArena> m_wideText;
    AString<Alloc::GlobalArena> m_utf8Text;
    AString<Alloc::GlobalArena> m_preeditText;
    TextInputSessionToken m_compositionToken;
    u32 m_pendingHighSurrogate = 0u;
    u32 m_pendingPreeditHighSurrogate = 0u;
    usize m_preeditCaretByte = 0u;
    bool m_pendingPreeditMoveCaret = true;
    bool m_imeAvailable = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

