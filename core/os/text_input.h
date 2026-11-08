// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/general.h>

#include <core/os/text_input_types.adhd.h>

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextInputBackend{
    enum Enum : u8{
        Unsupported = NWB::Core::OsTextInputTypes::TextInputBackend::Unsupported,
        Win32Imm32 = NWB::Core::OsTextInputTypes::TextInputBackend::Win32Imm32,
        X11Xim = NWB::Core::OsTextInputTypes::TextInputBackend::X11Xim,
        Wayland = NWB::Core::OsTextInputTypes::TextInputBackend::Wayland,
    };
};

namespace TextInputAdmission{
    enum Enum : u8{
        Accepted = NWB::Core::OsTextInputTypes::TextInputAdmission::Accepted,
        WrongThread = NWB::Core::OsTextInputTypes::TextInputAdmission::WrongThread,
        InvalidSession = NWB::Core::OsTextInputTypes::TextInputAdmission::InvalidSession,
        InvalidText = NWB::Core::OsTextInputTypes::TextInputAdmission::InvalidText,
        InvalidRange = NWB::Core::OsTextInputTypes::TextInputAdmission::InvalidRange,
        TooLarge = NWB::Core::OsTextInputTypes::TextInputAdmission::TooLarge,
        QueueFull = NWB::Core::OsTextInputTypes::TextInputAdmission::QueueFull,
        Unsupported = NWB::Core::OsTextInputTypes::TextInputAdmission::Unsupported,
        Unavailable = NWB::Core::OsTextInputTypes::TextInputAdmission::Unavailable,
        Busy = NWB::Core::OsTextInputTypes::TextInputAdmission::Busy,
        NativeFailure = NWB::Core::OsTextInputTypes::TextInputAdmission::NativeFailure,
    };
};

namespace TextInputChangeCause{
    enum Enum : u8{
        InputMethod = NWB::Core::OsTextInputTypes::TextInputChangeCause::InputMethod,
        Other = NWB::Core::OsTextInputTypes::TextInputChangeCause::Other,
    };
};

namespace TextInputDeletionBasis{
    enum Enum : u8{
        Caret,
        Selection,
        Count,
    };
};

namespace TextInputPollResult{
    enum Enum : u8{
        InvalidSession = NWB::Core::OsTextInputTypes::TextInputPollResult::InvalidSession,
        Pending = NWB::Core::OsTextInputTypes::TextInputPollResult::Pending,
        Event = NWB::Core::OsTextInputTypes::TextInputPollResult::Event,
        WrongThread = NWB::Core::OsTextInputTypes::TextInputPollResult::WrongThread,
    };
};

namespace TextInputEventKind{
    enum Enum : u8{
        Commit = NWB::Core::OsTextInputTypes::TextInputEventKind::Commit,
        Preedit = NWB::Core::OsTextInputTypes::TextInputEventKind::Preedit,
        DeleteSurrounding = NWB::Core::OsTextInputTypes::TextInputEventKind::DeleteSurrounding,
        Cancelled = NWB::Core::OsTextInputTypes::TextInputEventKind::Cancelled,
    };
};

namespace TextInputCancelReason{
    enum Enum : u8{
        FocusLost = NWB::Core::OsTextInputTypes::TextInputCancelReason::FocusLost,
        NativeCancelled = NWB::Core::OsTextInputTypes::TextInputCancelReason::NativeCancelled,
        Overflow = NWB::Core::OsTextInputTypes::TextInputCancelReason::Overflow,
        NativeFailure = NWB::Core::OsTextInputTypes::TextInputCancelReason::NativeFailure,
    };
};

inline constexpr usize s_TextInputMaxEvents = NWB::Core::OsTextInputTypes::s_TextInputMaxEvents;
inline constexpr usize s_TextInputMaxEventTextBytes = NWB::Core::OsTextInputTypes::s_TextInputMaxEventTextBytes;
inline constexpr usize s_TextInputMaxQueuedTextBytes = NWB::Core::OsTextInputTypes::s_TextInputMaxQueuedTextBytes;
inline constexpr usize s_TextInputMaxSurroundingBytes = NWB::Core::OsTextInputTypes::s_TextInputMaxSurroundingBytes;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TextInputCapabilities{
    b8 commit = false;
    b8 preedit = false;
    b8 surrounding = false;
    b8 deleteSurrounding = false;
    TextInputBackend::Enum backend = TextInputBackend::Unsupported;
};

struct TextInputSessionToken{
    u64 service = 0u;
    u64 generation = 0u;


public:
    [[nodiscard]] b8 valid()const noexcept{ return service != 0u && generation != 0u; }
};

[[nodiscard]] inline b8 operator==(const TextInputSessionToken& lhs, const TextInputSessionToken& rhs)noexcept{
    return lhs.service == rhs.service && lhs.generation == rhs.generation;
}

// Coordinates are native window-client pixels. Linux surface coordinates are converted by the backend.
using TextInputRect = NWB::Core::OsTextInputTypes::TextInputRect;

struct TextInputSessionDesc{
    TextInputRect caret;
    AStringView surrounding;
    usize anchorByte = 0u;
    usize caretByte = 0u;
};

struct TextInputEvent{
    AString<Alloc::GlobalArena> text;
    TextInputSessionToken token;
    u64 sequence = 0u;
    u64 surroundingRevision = 0u;
    usize anchorByte = 0u;
    usize caretByte = 0u;
    usize deleteBeforeBytes = 0u;
    usize deleteAfterBytes = 0u;
    TextInputEventKind::Enum kind = TextInputEventKind::Commit;
    b8 caretVisible = true;
    TextInputCancelReason::Enum cancelReason = TextInputCancelReason::NativeCancelled;
    TextInputDeletionBasis::Enum deletionBasis = TextInputDeletionBasis::Caret;


public:
    explicit TextInputEvent(Alloc::GlobalArena& arena)noexcept(noexcept(AString<Alloc::GlobalArena>(arena)))
        : text(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Construct, update, native dispatch, poll and destroy on the owning OS/event thread.
// No UI callback or model is borrowed.
// begin/update copy valid UTF-8 without embedded NUL.
// All text positions are UTF-8 byte boundaries, never code-point counts.
// Preedit positions index event.text; deletion addresses the snapshot at surroundingRevision.
// Caret distances address the caret; Selection distances skip the selected range and preserve its bytes.
// poll consumes an event and copies its text into the caller's arena.
// A service admits only one active session.
// Focus loss or event overflow discards pending events and leaves one terminal Cancelled event for that token.
// A cancelled token can poll that event once; end or a new begin invalidates it.
// Stale/foreign native delivery is rejected.
interface ITextInputService : private NoCopy{
public:
    virtual ~ITextInputService()noexcept = default;


public:
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept = 0;
    [[nodiscard]] virtual b8 isOwnerThread()const noexcept = 0;
    [[nodiscard]] virtual TextInputSessionToken activeSession()const noexcept = 0;
    [[nodiscard]] virtual u64 surroundingRevision(TextInputSessionToken token)const noexcept = 0;
    [[nodiscard]] virtual Expected<TextInputSessionToken, TextInputAdmission::Enum> begin(const TextInputSessionDesc& desc) = 0;
    [[nodiscard]] virtual b8 end(TextInputSessionToken token) = 0;
    [[nodiscard]] virtual TextInputAdmission::Enum updateCaret(TextInputSessionToken token, TextInputRect caret) = 0;
    [[nodiscard]] virtual TextInputAdmission::Enum updateSurrounding(TextInputSessionToken token, AStringView text, usize anchorByte, usize caretByte, TextInputChangeCause::Enum cause = TextInputChangeCause::Other) = 0;
    [[nodiscard]] virtual TextInputPollResult::Enum poll(TextInputSessionToken token, TextInputEvent& event) = 0;
    // Called by Frame's native focus path before ordinary input focus dispatch. Initially unfocused.
    [[nodiscard]] virtual b8 setFocused(b8 focused) = 0;
};

// The native HWND is borrowed until service destruction.
// nullptr returns an explicit unsupported service.
// Native Linux factories and event bridges belong to their X11/Wayland domains.
[[nodiscard]] Expected<GlobalUniquePtr<ITextInputService>> CreateTextInputService(Alloc::GlobalArena& arena, void* nativeWindowHandle);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

