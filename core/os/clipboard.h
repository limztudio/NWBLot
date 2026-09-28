// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/general.h>


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
    enum Enum : u8{ Accepted, WrongThread, QueueFull };
};

namespace ClipboardPollResult{
    enum Enum : u8{ InvalidRequest, Pending, Completed, WrongThread };
};

inline constexpr usize s_ClipboardMaxOutstandingRequests = 32u;
inline constexpr usize s_ClipboardMaxTextBytes = 16u * 1024u * 1024u;

struct ClipboardCapabilities{
    bool readText = false;
    bool writeText = false;
};

struct ClipboardRequestToken{
    u64 service = 0u;
    u64 generation = 0u;

    [[nodiscard]] bool valid()const noexcept{ return service != 0u && generation != 0u; }
};

[[nodiscard]] inline bool operator==(const ClipboardRequestToken& lhs, const ClipboardRequestToken& rhs)noexcept{
    return lhs.service == rhs.service && lhs.generation == rhs.generation;
}

struct ClipboardRequestResult{
    ClipboardRequestToken token;
    ClipboardAdmission::Enum admission = ClipboardAdmission::QueueFull;
};

struct ClipboardCompletion{
    AString<Alloc::GlobalArena> text;
    ClipboardRequestToken token;
    ClipboardOperation::Enum operation = ClipboardOperation::ReadText;
    ClipboardChannel::Enum channel = ClipboardChannel::Clipboard;
    ClipboardStatus::Enum status = ClipboardStatus::Unavailable;

    explicit ClipboardCompletion(Alloc::GlobalArena& arena)
        : text(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Construct, request, pump, poll, cancel and destroy on the owning OS/event thread. There are no client callbacks.
// Requests own copied UTF-8 bytes; completions own their text in the caller's arena. Poll consumes a completed token.
// Text excludes embedded NUL. Native Windows writes use CRLF and reads normalize CRLF to LF.
// Cancel invalidates pending or completed delivery, but cannot undo a write already performed by pump(). Destroy
// cancels all undelivered work. Tokens never reference client objects and stale/foreign tokens are rejected.
class IClipboardService : NoCopy{
public:
    virtual ~IClipboardService() = default;


public:
    [[nodiscard]] virtual ClipboardCapabilities capabilities(ClipboardChannel::Enum channel)const noexcept = 0;
    [[nodiscard]] virtual bool isOwnerThread()const noexcept = 0;
    [[nodiscard]] virtual ClipboardRequestResult requestReadText(ClipboardChannel::Enum channel) = 0;
    [[nodiscard]] virtual ClipboardRequestResult requestWriteText(ClipboardChannel::Enum channel, AStringView text) = 0;
    [[nodiscard]] virtual ClipboardPollResult::Enum poll(ClipboardRequestToken token, ClipboardCompletion& completion) = 0;
    [[nodiscard]] virtual bool cancel(ClipboardRequestToken token) = 0;
    // Executes already-admitted operations in order, without retries. Recursive pump calls return false.
    [[nodiscard]] virtual bool pump() = 0;
};

// The native window is borrowed until service destruction. Windows requires a live owner HWND for writes;
// nullptr selects an explicitly unsupported service. Other platforms currently return that unsupported service.
[[nodiscard]] GlobalUniquePtr<IClipboardService> CreateClipboardService(Alloc::GlobalArena& arena, void* nativeWindowHandle);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

