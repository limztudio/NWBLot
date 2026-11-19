// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/general.h>

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ClipboardChannel{
    enum Enum : u8{ Clipboard, PrimarySelection };
};

namespace ClipboardOperation{
    enum Enum : u8{ ReadText, WriteText };
};

namespace ClipboardStatus{
    enum Enum : u8{ Success, Unsupported, Unavailable, InvalidText, TooLarge, NativeFailure };
};

namespace ClipboardAdmission{
    enum Enum : u8{ WrongThread, QueueFull };
};

namespace ClipboardPollResult{
    enum Enum : u8{ InvalidRequest, Pending, Completed, WrongThread };
};

inline constexpr usize s_ClipboardMaxOutstandingRequests = 32u;
inline constexpr usize s_ClipboardMaxTextBytes = 16u * 1024u * 1024u;
inline constexpr usize s_ClipboardChannelCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ClipboardCapabilities{
    b8 readText = false;
    b8 writeText = false;
};

struct ClipboardRequestToken{
    u64 service = 0u;
    u64 generation = 0u;


public:
    [[nodiscard]] b8 valid()const noexcept{ return service != 0u && generation != 0u; }
};

[[nodiscard]] inline b8 operator==(const ClipboardRequestToken& lhs, const ClipboardRequestToken& rhs)noexcept{
    return lhs.service == rhs.service && lhs.generation == rhs.generation;
}

struct ClipboardCompletion{
    AString<::ArenaBase> text;
    ClipboardRequestToken token;
    ClipboardOperation::Enum operation = ClipboardOperation::ReadText;
    ClipboardChannel::Enum channel = ClipboardChannel::Clipboard;
    ClipboardStatus::Enum status = ClipboardStatus::Unavailable;


public:
    explicit ClipboardCompletion(::ArenaBase& arena)noexcept
        : text(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


interface IClipboardService : private NoCopy{
public:
    virtual ~IClipboardService()noexcept = default;


public:
    [[nodiscard]] virtual ClipboardCapabilities capabilities(ClipboardChannel::Enum channel)const noexcept = 0;
    [[nodiscard]] virtual b8 isOwnerThread()const noexcept = 0;
    [[nodiscard]] virtual Expected<ClipboardRequestToken, ClipboardAdmission::Enum> requestReadText(ClipboardChannel::Enum channel) = 0;
    [[nodiscard]] virtual Expected<ClipboardRequestToken, ClipboardAdmission::Enum> requestWriteText(ClipboardChannel::Enum channel, AStringView text) = 0;
    [[nodiscard]] virtual ClipboardPollResult::Enum poll(ClipboardRequestToken token, ClipboardCompletion& completion) = 0;
    [[nodiscard]] virtual b8 cancel(ClipboardRequestToken token) = 0;
    [[nodiscard]] virtual b8 pump() = 0;
};

[[nodiscard]] Expected<GlobalUniquePtr<IClipboardService>> CreateClipboardService(Alloc::GlobalArena& arena, void* nativeWindowHandle);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

