// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/os/clipboard_service.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Win32ClipboardService final : public QueuedClipboardService{
public:
    Win32ClipboardService(Alloc::GlobalArena& arena, NotNull<void*> nativeWindowHandle);


public:
    [[nodiscard]] virtual ClipboardCapabilities capabilities(ClipboardChannel::Enum channel)const noexcept override;


protected:
    virtual void startNativeRequest(ClipboardRequestToken token, ClipboardOperation::Enum operation, ClipboardChannel::Enum channel, AStringView text)override;
    virtual void cancelNativeRequest(ClipboardRequestToken token)noexcept override;


private:
    [[nodiscard]] ClipboardStatus::Enum readNativeText(AString<Alloc::GlobalArena>& text);
    [[nodiscard]] ClipboardStatus::Enum writeNativeText(AStringView text);


private:
    NotNull<void*> m_nativeWindowHandle;
    AString<Alloc::GlobalArena> m_utf8Text;
    WString<Alloc::GlobalArena> m_wideText;
    ClipboardRequestToken m_nativeToken;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

