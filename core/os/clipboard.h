// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/general.h>

#include <core/os/clipboard_types.adhd.h>

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ClipboardChannel{
    enum Enum : u8{
        Clipboard = NWB::Core::OsClipboardTypes::ClipboardChannel::Clipboard,
        PrimarySelection = NWB::Core::OsClipboardTypes::ClipboardChannel::PrimarySelection,
    };
};

namespace ClipboardOperation{
    enum Enum : u8{
        ReadText = NWB::Core::OsClipboardTypes::ClipboardOperation::ReadText,
        WriteText = NWB::Core::OsClipboardTypes::ClipboardOperation::WriteText,
    };
};

namespace ClipboardStatus{
    enum Enum : u8{
        Success = NWB::Core::OsClipboardTypes::ClipboardStatus::Success,
        Unsupported = NWB::Core::OsClipboardTypes::ClipboardStatus::Unsupported,
        Unavailable = NWB::Core::OsClipboardTypes::ClipboardStatus::Unavailable,
        InvalidText = NWB::Core::OsClipboardTypes::ClipboardStatus::InvalidText,
        TooLarge = NWB::Core::OsClipboardTypes::ClipboardStatus::TooLarge,
        NativeFailure = NWB::Core::OsClipboardTypes::ClipboardStatus::NativeFailure,
    };
};

namespace ClipboardAdmission{
    enum Enum : u8{
        WrongThread = NWB::Core::OsClipboardTypes::ClipboardAdmission::WrongThread,
        QueueFull = NWB::Core::OsClipboardTypes::ClipboardAdmission::QueueFull,
    };
};

namespace ClipboardPollResult{
    enum Enum : u8{
        InvalidRequest = NWB::Core::OsClipboardTypes::ClipboardPollResult::InvalidRequest,
        Pending = NWB::Core::OsClipboardTypes::ClipboardPollResult::Pending,
        Completed = NWB::Core::OsClipboardTypes::ClipboardPollResult::Completed,
        WrongThread = NWB::Core::OsClipboardTypes::ClipboardPollResult::WrongThread,
    };
};

inline constexpr usize s_ClipboardMaxOutstandingRequests = NWB::Core::OsClipboardTypes::s_ClipboardMaxOutstandingRequests;
inline constexpr usize s_ClipboardMaxTextBytes = NWB::Core::OsClipboardTypes::s_ClipboardMaxTextBytes;
inline constexpr usize s_ClipboardChannelCount = NWB::Core::OsClipboardTypes::s_ClipboardChannelCount;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ClipboardCapabilities = NWB::Core::OsClipboardTypes::ClipboardCapabilities;

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
    AString<Alloc::GlobalArena> text;
    ClipboardRequestToken token;
    ClipboardOperation::Enum operation = ClipboardOperation::ReadText;
    ClipboardChannel::Enum channel = ClipboardChannel::Clipboard;
    ClipboardStatus::Enum status = ClipboardStatus::Unavailable;


public:
    explicit ClipboardCompletion(Alloc::GlobalArena& arena)noexcept
        : text(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Construct, request, pump, poll, cancel and destroy on the owning OS/event thread.
// There are no client callbacks.
// Requests own copied UTF-8 bytes; completions own their text in the caller's arena.
// Poll consumes a completed token.
// Text excludes embedded NUL.
// Native Windows writes use CRLF and reads normalize CRLF to LF.
// Cancel invalidates pending or completed delivery, but cannot undo a write already performed by pump().
// Destroy cancels all undelivered work.
// Tokens never reference client objects and stale/foreign tokens are rejected.
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
    // Starts admitted operations in order and advances native transfers without waiting for another client.
    // A pending native operation preserves FIFO order; cancellation releases it.
    // Recursive pumps return false.
    [[nodiscard]] virtual b8 pump() = 0;
};

// The native window is borrowed until service destruction.
// Windows requires a live owner HWND for writes; nullptr selects an explicitly unsupported service.
// Linux native factories live in the X11/Wayland OS domains.
[[nodiscard]] Expected<GlobalUniquePtr<IClipboardService>> CreateClipboardService(Alloc::GlobalArena& arena, void* nativeWindowHandle);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

