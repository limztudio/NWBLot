// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <logger/common.h>

#include <global/diagnostics.h>
#include <global/filesystem/operations.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using MessageItem = Pair<LogString, Core::Common::LogType::Enum>;
using MessageDeque = Deque<MessageItem, LogArena>;

inline constexpr AStringView s_ClipboardDiagnosticCategory = "logger_clipboard";
inline constexpr usize s_ClipboardLineBreakCharacterCount = 2u;

class ClipboardText final : NoCopy{
public:
    explicit ClipboardText(HGLOBAL memory)noexcept
        : m_memory(memory)
    {}
    ClipboardText(ClipboardText&& other)noexcept
        : m_memory(Exchange(other.m_memory, nullptr))
    {}
    ~ClipboardText()noexcept{
        const auto closed = close();
        if(!closed){
            const DiagnosticEventText detail = MakeDiagnosticEventText("Failed to release untransferred clipboard memory: native_category={} native_code={}", closed.error().category().name(), closed.error().value());
            CaptureDiagnosticEvent(s_ClipboardDiagnosticCategory, detail.view(), __FILE__, __LINE__);
        }
    }


public:
    [[nodiscard]] HGLOBAL native()const noexcept{ return m_memory; }
    void release()noexcept{ m_memory = nullptr; }
    [[nodiscard]] Expected<void, ErrorCode> close()noexcept{
        if(!m_memory)
            return {};
        if(GlobalFree(m_memory))
            return MakeUnexpected(GlobalFilesystemDetail::LastSystemError());
        m_memory = nullptr;
        return {};
    }


private:
    HGLOBAL m_memory;
};

struct ClipboardTextFailure{
    AStringView operation;
    ErrorCode code;
    Optional<ClipboardText> memory{};
};

[[nodiscard]] inline Expected<usize, ErrorCode> ClipboardByteCount(const usize characterCount)noexcept{
    if(characterCount > (Limit<usize>::s_Max / sizeof(tchar)) - 1u)
        return MakeUnexpected(MakeErrorCode(ErrorCondition::value_too_large));
    return (characterCount + 1u) * sizeof(tchar);
}

[[nodiscard]] inline Expected<ClipboardText, ClipboardTextFailure> BuildClipboardText(const MessageDeque& messages)noexcept{
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
        return MakeUnexpected(ClipboardTextFailure{ .operation = "allocate_clipboard_text", .code = GlobalFilesystemDetail::LastSystemError(), });
    ClipboardText memory(nativeMemory);
    auto* const destination = static_cast<tchar*>(GlobalLock(nativeMemory));
    if(!destination)
        return MakeUnexpected(ClipboardTextFailure{ .operation = "lock_clipboard_text", .code = GlobalFilesystemDetail::LastSystemError(), .memory = Move(memory), });
    usize cursor = 0u;
    for(const auto& message : messages){
        const LogString& text = message.first();
        const usize textBytes = text.size() * sizeof(tchar);
        NWB_MEMCPY(destination + cursor, *byteSize - cursor * sizeof(tchar), text.data(), textBytes);
        cursor += text.size();
        destination[cursor++] = static_cast<tchar>('\r');
        destination[cursor++] = static_cast<tchar>('\n');
    }
    destination[cursor] = tchar{};
    SetLastError(NO_ERROR);
    if(!GlobalUnlock(nativeMemory) && GetLastError() != NO_ERROR)
        return MakeUnexpected(ClipboardTextFailure{ .operation = "unlock_clipboard_text", .code = GlobalFilesystemDetail::LastSystemError(), .memory = Move(memory), });
    return memory;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif //NWB_PLATFORM_WINDOWS


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

