// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "clipboard_service.h"

#include <core/common/log.h>

#include <windows.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_win32_clipboard{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ClipboardCloseGuard final : NoCopy{
public:
    ~ClipboardCloseGuard(){
        if(!CloseClipboard())
            NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: CloseClipboard failed ({})"), GetLastError());
    }
};

class GlobalUnlockGuard final : NoCopy{
public:
    explicit GlobalUnlockGuard(const HGLOBAL memory)
        : m_memory(memory)
    {}
    ~GlobalUnlockGuard(){
        SetLastError(ERROR_SUCCESS);
        if(!GlobalUnlock(m_memory) && GetLastError() != ERROR_SUCCESS)
            NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: GlobalUnlock failed ({})"), GetLastError());
    }


private:
    HGLOBAL m_memory;
};

class GlobalMemoryOwner final : NoCopy{
public:
    explicit GlobalMemoryOwner(const HGLOBAL memory)
        : m_memory(memory)
    {}
    ~GlobalMemoryOwner(){
        if(m_memory && GlobalFree(m_memory))
            NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: GlobalFree failed ({})"), GetLastError());
    }


public:
    void release()noexcept{ m_memory = nullptr; }


private:
    HGLOBAL m_memory;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Win32ClipboardService::Win32ClipboardService(Alloc::GlobalArena& arena, const NotNull<void*> nativeWindowHandle)
    : QueuedClipboardService(arena)
    , m_nativeWindowHandle(nativeWindowHandle)
    , m_utf8Text(arena)
    , m_wideText(arena)
{}

ClipboardCapabilities Win32ClipboardService::capabilities(const ClipboardChannel::Enum channel)const noexcept{
    return channel == ClipboardChannel::Clipboard ? ClipboardCapabilities{ .readText = true, .writeText = true } : ClipboardCapabilities{};
}

void Win32ClipboardService::startNativeRequest(
    const ClipboardRequestToken token,
    const ClipboardOperation::Enum operation,
    const ClipboardChannel::Enum,
    const AStringView text){
    m_nativeToken = token;
    m_utf8Text.clear();
    const ClipboardStatus::Enum status = operation == ClipboardOperation::ReadText
        ? readNativeText(m_utf8Text)
        : writeNativeText(text)
    ;
    if(m_nativeToken == token){
        m_nativeToken = {};
        if(!completeNativeRequest(token, status, m_utf8Text))
            GLB_FATAL_ASSERT(false);
    }
    m_utf8Text.clear();
}

void Win32ClipboardService::cancelNativeRequest(const ClipboardRequestToken token){
    if(m_nativeToken == token)
        m_nativeToken = {};
}

ClipboardStatus::Enum Win32ClipboardService::readNativeText(AString<Alloc::GlobalArena>& text){
    using namespace __hidden_win32_clipboard;
    if(!OpenClipboard(static_cast<HWND>(m_nativeWindowHandle.get()))){
        NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: OpenClipboard unavailable ({})"), GetLastError());
        return ClipboardStatus::Unavailable;
    }
    ClipboardCloseGuard closeGuard;
    if(!IsClipboardFormatAvailable(CF_UNICODETEXT))
        return ClipboardStatus::Unavailable;

    const HGLOBAL memory = GetClipboardData(CF_UNICODETEXT);
    if(!memory){
        NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: GetClipboardData failed ({})"), GetLastError());
        return ClipboardStatus::NativeFailure;
    }
    const usize bytes = GlobalSize(memory);
    if(bytes < sizeof(wchar) || (bytes % sizeof(wchar)) != 0u){
        NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: invalid Unicode clipboard allocation"));
        return ClipboardStatus::InvalidText;
    }
    if(bytes > (s_ClipboardMaxTextBytes * 2u + 1u) * sizeof(wchar))
        return ClipboardStatus::TooLarge;
    const wchar* const wide = static_cast<const wchar*>(GlobalLock(memory));
    if(!wide){
        NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: GlobalLock failed ({})"), GetLastError());
        return ClipboardStatus::NativeFailure;
    }
    GlobalUnlockGuard unlockGuard(memory);
    const usize capacity = bytes / sizeof(wchar);
    usize length = 0u;
    while(length < capacity && wide[length] != L'\0')
        ++length;
    if(length == capacity){
        NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: Unicode clipboard text is not terminated"));
        return ClipboardStatus::InvalidText;
    }
    if(length == 0u){
        text.clear();
        return ClipboardStatus::Success;
    }
    const i32 utf8Length = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wide, static_cast<i32>(length), nullptr, 0, nullptr, nullptr
    );
    if(utf8Length == 0){
        NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: invalid Unicode text ({})"), GetLastError());
        return ClipboardStatus::InvalidText;
    }
    if(static_cast<usize>(utf8Length) > s_ClipboardMaxTextBytes * 2u)
        return ClipboardStatus::TooLarge;
    text.resize(static_cast<usize>(utf8Length));
    const i32 convertedBytes = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wide, static_cast<i32>(length), text.data(), utf8Length, nullptr, nullptr
    );
    if(convertedBytes != utf8Length){
        NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: Unicode conversion failed ({})"), GetLastError());
        return ClipboardStatus::NativeFailure;
    }
    usize destination = 0u;
    for(usize source = 0u; source < text.size(); ++source){
        if(text[source] == '\r' && source + 1u < text.size() && text[source + 1u] == '\n')
            continue;
        text[destination] = text[source];
        ++destination;
    }
    text.resize(destination);
    if(text.size() > s_ClipboardMaxTextBytes)
        return ClipboardStatus::TooLarge;
    return ClipboardStatus::Success;
}

ClipboardStatus::Enum Win32ClipboardService::writeNativeText(const AStringView text){
    using namespace __hidden_win32_clipboard;
    m_wideText.clear();
    if(!text.empty()){
        const i32 length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<i32>(text.size()), nullptr, 0);
        if(length == 0){
            NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: invalid UTF-8 text ({})"), GetLastError());
            return ClipboardStatus::InvalidText;
        }
        m_wideText.resize(static_cast<usize>(length));
        const i32 convertedLength = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<i32>(text.size()), m_wideText.data(), length
        );
        if(convertedLength != length){
            NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: UTF-8 conversion failed ({})"), GetLastError());
            return ClipboardStatus::NativeFailure;
        }
    }

    // Allocate and validate before replacing the current clipboard. SetClipboardData transfers this allocation.
    usize extraCarriageReturns = 0u;
    for(usize index = 0u; index < m_wideText.size(); ++index){
        if(m_wideText[index] == L'\n' && (index == 0u || m_wideText[index - 1u] != L'\r'))
            ++extraCarriageReturns;
    }
    const usize bytes = (m_wideText.size() + extraCarriageReturns + 1u) * sizeof(wchar);
    const HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if(!memory){
        NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: GlobalAlloc failed ({})"), GetLastError());
        return ClipboardStatus::NativeFailure;
    }
    GlobalMemoryOwner memoryOwner(memory);
    wchar* const destination = static_cast<wchar*>(GlobalLock(memory));
    if(!destination){
        NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: GlobalLock failed ({})"), GetLastError());
        return ClipboardStatus::NativeFailure;
    }
    {
        GlobalUnlockGuard unlockGuard(memory);

        usize output = 0u;
        for(usize index = 0u; index < m_wideText.size(); ++index){
            if(m_wideText[index] == L'\n' && (index == 0u || m_wideText[index - 1u] != L'\r')){
                destination[output] = L'\r';
                ++output;
            }
            destination[output] = m_wideText[index];
            ++output;
        }
        destination[output] = L'\0';
    }
    if(!OpenClipboard(static_cast<HWND>(m_nativeWindowHandle.get()))){
        NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: OpenClipboard unavailable ({})"), GetLastError());
        return ClipboardStatus::Unavailable;
    }
    ClipboardCloseGuard closeGuard;
    if(!EmptyClipboard()){
        NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: EmptyClipboard failed ({})"), GetLastError());
        return ClipboardStatus::NativeFailure;
    }
    if(!SetClipboardData(CF_UNICODETEXT, memory)){
        NWB_LOGGER_WARNING(GLB_TEXT("Clipboard: SetClipboardData failed ({})"), GetLastError());
        return ClipboardStatus::NativeFailure;
    }
    memoryOwner.release();
    return ClipboardStatus::Success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

