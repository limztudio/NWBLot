// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "clipboard_text_internal.h"

#if defined(FOUNDATION_PLATFORM_WINDOWS)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ClipboardText::~ClipboardText()noexcept{
    const auto closed = close();
    if(!closed){
        const DiagnosticEventText detail = MakeDiagnosticEventText("Failed to release untransferred clipboard memory: native_category={} native_code={}", closed.error().category().name(), closed.error().value());
        CaptureDiagnosticEvent(s_ClipboardDiagnosticCategory, detail.view(), __FILE__, __LINE__);
    }
}

Expected<void, ErrorCode> ClipboardText::close()noexcept{
    if(!m_memory)
        return {};
    if(GlobalFree(m_memory))
        return MakeUnexpected(LastSystemError());
    m_memory = nullptr;
    return {};
}

Expected<ClipboardText, ClipboardTextFailure> BuildClipboardText(const MessageDeque& messages)noexcept{
    usize characterCount = 0u;
    for(const auto& message : messages){
        if(!AddBinaryReserveBytes(characterCount, message.first().size()) || !AddBinaryReserveBytes(characterCount, s_ClipboardLineBreakCharacterCount))
            return MakeUnexpected(ClipboardTextFailure{ .operation = "count_clipboard_text", .code = MakeErrorCode(ErrorCondition::value_too_large), });
    }
    const auto byteSize = ClipboardByteCount(characterCount);
    if(!byteSize)
        return MakeUnexpected(ClipboardTextFailure{ .operation = "size_clipboard_text", .code = byteSize.error(), });
    const HGLOBAL nativeMemory = GlobalAlloc(GMEM_MOVEABLE, *byteSize);
    if(!nativeMemory)
        return MakeUnexpected(ClipboardTextFailure{ .operation = "allocate_clipboard_text", .code = LastSystemError(), });
    ClipboardText memory(nativeMemory);
    auto* const destination = static_cast<tchar*>(GlobalLock(nativeMemory));
    if(!destination)
        return MakeUnexpected(ClipboardTextFailure{ .operation = "lock_clipboard_text", .code = LastSystemError(), .memory = Move(memory), });
    usize cursor = 0u;
    for(const auto& message : messages){
        const LogString& text = message.first();
        const usize textBytes = text.size() * sizeof(tchar);
        FOUNDATION_MEMCPY(destination + cursor, *byteSize - cursor * sizeof(tchar), text.data(), textBytes);
        cursor += text.size();
        destination[cursor++] = static_cast<tchar>('\r');
        destination[cursor++] = static_cast<tchar>('\n');
    }
    destination[cursor] = tchar{};
    SetLastError(NO_ERROR);
    if(!GlobalUnlock(nativeMemory) && GetLastError() != NO_ERROR)
        return MakeUnexpected(ClipboardTextFailure{ .operation = "unlock_clipboard_text", .code = LastSystemError(), .memory = Move(memory), });
    return memory;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


#endif // FOUNDATION_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

