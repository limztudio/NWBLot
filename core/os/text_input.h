// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/general.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextInputBackend{
    enum Enum : u8{ Unsupported, Win32Imm32, X11Xim, Wayland };
};

namespace TextInputAdmission{
    enum Enum : u8{
        Accepted, WrongThread, InvalidSession, InvalidText, InvalidRange, TooLarge, QueueFull,
        Unsupported, Unavailable, Busy, NativeFailure
    };
};

namespace TextInputChangeCause{
    enum Enum : u8{ InputMethod, Other };
};

namespace TextInputDeletionBasis{
    enum Enum : u8{ Caret, Selection, kCount };
};

namespace TextInputPollResult{
    enum Enum : u8{ InvalidSession, Pending, Event, WrongThread };
};

namespace TextInputEventKind{
    enum Enum : u8{ Commit, Preedit, DeleteSurrounding, Cancelled };
};

namespace TextInputCancelReason{
    enum Enum : u8{ FocusLost, NativeCancelled, Overflow, NativeFailure };
};

inline constexpr usize s_TextInputMaxEvents = 64u;
inline constexpr usize s_TextInputMaxEventTextBytes = 64u * 1024u;
inline constexpr usize s_TextInputMaxQueuedTextBytes = 256u * 1024u;
inline constexpr usize s_TextInputMaxSurroundingBytes = 1024u * 1024u;

struct TextInputCapabilities{
    bool commit = false;
    bool preedit = false;
    bool surrounding = false;
    bool deleteSurrounding = false;
    TextInputBackend::Enum backend = TextInputBackend::Unsupported;
};

struct TextInputSessionToken{
    u64 service = 0u;
    u64 generation = 0u;

    [[nodiscard]] bool valid()const noexcept{ return service != 0u && generation != 0u; }
};

[[nodiscard]] inline bool operator==(const TextInputSessionToken& lhs, const TextInputSessionToken& rhs)noexcept{
    return lhs.service == rhs.service && lhs.generation == rhs.generation;
}

// Coordinates are native window-client pixels. Linux surface coordinates are converted by the backend.
struct TextInputRect{
    i32 x = 0;
    i32 y = 0;
    i32 width = 1;
    i32 height = 1;
};

struct TextInputSessionDesc{
    TextInputRect caret;
    AStringView surrounding;
    usize anchorByte = 0u;
    usize caretByte = 0u;
};

struct TextInputBeginResult{
    TextInputSessionToken token;
    TextInputAdmission::Enum admission = TextInputAdmission::Unavailable;
};

struct TextInputEvent{
    AString<Alloc::GlobalArena> text;
    TextInputSessionToken token;
    u64 sequence = 0u;
    u64 surroundingRevision = 0u;
    TextInputEventKind::Enum kind = TextInputEventKind::Commit;
    usize anchorByte = 0u;
    usize caretByte = 0u;
    usize deleteBeforeBytes = 0u;
    usize deleteAfterBytes = 0u;
    bool caretVisible = true;
    TextInputCancelReason::Enum cancelReason = TextInputCancelReason::NativeCancelled;
    TextInputDeletionBasis::Enum deletionBasis = TextInputDeletionBasis::Caret;

    explicit TextInputEvent(Alloc::GlobalArena& arena)
        : text(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Construct, update, native dispatch, poll and destroy on the owning OS/event thread. No UI callback or model is borrowed.
// begin/update copy valid UTF-8 without embedded NUL. All text positions are UTF-8 byte boundaries, never code-point counts.
// Preedit positions index event.text; deletion addresses the snapshot at surroundingRevision.
// Caret distances address the caret; Selection distances skip the selected range and preserve its bytes.
// poll consumes an event and copies its text into the caller's arena. A service admits only one active session.
// Focus loss or event overflow discards pending events and leaves one terminal Cancelled event for that token.
// A cancelled token can poll that event once; end or a new begin invalidates it. Stale/foreign native delivery is rejected.
struct ITextInputService : private NoCopy{
public:
    virtual ~ITextInputService() = default;


public:
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept = 0;
    [[nodiscard]] virtual bool isOwnerThread()const noexcept = 0;
    [[nodiscard]] virtual TextInputSessionToken activeSession()const noexcept = 0;
    [[nodiscard]] virtual u64 surroundingRevision(TextInputSessionToken token)const noexcept = 0;
    [[nodiscard]] virtual TextInputBeginResult begin(const TextInputSessionDesc& desc) = 0;
    [[nodiscard]] virtual bool end(TextInputSessionToken token) = 0;
    [[nodiscard]] virtual TextInputAdmission::Enum updateCaret(TextInputSessionToken token, TextInputRect caret) = 0;
    [[nodiscard]] virtual TextInputAdmission::Enum updateSurrounding(
        TextInputSessionToken token, AStringView text, usize anchorByte, usize caretByte,
        TextInputChangeCause::Enum cause = TextInputChangeCause::Other
    ) = 0;
    [[nodiscard]] virtual TextInputPollResult::Enum poll(TextInputSessionToken token, TextInputEvent& event) = 0;
    // Called by Frame's native focus path before ordinary input focus dispatch. Initially unfocused.
    [[nodiscard]] virtual bool setFocused(bool focused) = 0;
};

// The native HWND is borrowed until service destruction. nullptr returns an explicit unsupported service.
// Native Linux factories and event bridges belong to their X11/Wayland domains.
[[nodiscard]] GlobalUniquePtr<ITextInputService> CreateTextInputService(Alloc::GlobalArena& arena, void* nativeWindowHandle);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

