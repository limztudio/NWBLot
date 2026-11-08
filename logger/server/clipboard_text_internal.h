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

inline constexpr AStringView s_ClipboardDiagnosticCategory = ::NWB::Log::s_LogServerValueClipboardDiagnosticCategory;
inline constexpr usize s_ClipboardLineBreakCharacterCount = ::NWB::Log::s_LogServerValueClipboardLineBreakCharacterCount;

class ClipboardText final : NoCopy{
public:
    explicit ClipboardText(HGLOBAL memory)noexcept
        : m_memory(memory)
    {}
    ClipboardText(ClipboardText&& other)noexcept
        : m_memory(Exchange(other.m_memory, nullptr))
    {}
    ~ClipboardText()noexcept;


public:
    [[nodiscard]] HGLOBAL native()const noexcept{ return m_memory; }

    void release()noexcept{ m_memory = nullptr; }

    [[nodiscard]] Expected<void, ErrorCode> close()noexcept;


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

[[nodiscard]] Expected<ClipboardText, ClipboardTextFailure> BuildClipboardText(const MessageDeque& messages)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

