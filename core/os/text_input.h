// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/general.h>

#include <global/expected.h>


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

namespace TextInputPreeditClauseStyle{
    enum Enum : u8{ Raw = 0u, Converted = 1u, Target = 2u, Selected = 3u, kCount };
};

struct TextInputPreeditClause{
    usize beginByte = 0u;
    usize endByte = 0u;
    TextInputPreeditClauseStyle::Enum style = TextInputPreeditClauseStyle::Raw;
};

inline constexpr usize s_TextInputMaxEvents = 64u;
inline constexpr usize s_TextInputMaxEventTextBytes = 64u * 1024u;
inline constexpr usize s_TextInputMaxQueuedTextBytes = 256u * 1024u;
inline constexpr usize s_TextInputMaxSurroundingBytes = 1024u * 1024u;


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

struct TextInputEvent{
    using uses_leading_arena_t = ::ArenaBase;

    AString<::ArenaBase> text;
    Vector<TextInputPreeditClause, ::ArenaBase> preeditClauses;
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
    explicit TextInputEvent(::ArenaBase& arena)noexcept(noexcept(AString<::ArenaBase>(arena)) && noexcept(Vector<TextInputPreeditClause, ::ArenaBase>(arena)))
        : text(arena)
        , preeditClauses(arena)
    {}
    TextInputEvent(const TextInputEvent&) = default;
    TextInputEvent(TextInputEvent&&)noexcept = default;
    TextInputEvent& operator=(const TextInputEvent&) = default;
    TextInputEvent(::ArenaBase& arena, const TextInputEvent& source) : TextInputEvent(arena){ *this = source; }
    TextInputEvent(::ArenaBase& arena, TextInputEvent&& source) : TextInputEvent(arena){ *this = Move(source); }

    TextInputEvent& operator=(TextInputEvent&& source){
        if(this != &source){
            if(text.get_allocator() == source.text.get_allocator())
                text = Move(source.text);
            else
                text.assign(source.text.data(), source.text.size());
            if(preeditClauses.get_allocator() == source.preeditClauses.get_allocator())
                preeditClauses = Move(source.preeditClauses);
            else
                preeditClauses.assign(source.preeditClauses.begin(), source.preeditClauses.end());
            token = source.token;
            sequence = source.sequence;
            surroundingRevision = source.surroundingRevision;
            anchorByte = source.anchorByte;
            caretByte = source.caretByte;
            deleteBeforeBytes = source.deleteBeforeBytes;
            deleteAfterBytes = source.deleteAfterBytes;
            kind = source.kind;
            caretVisible = source.caretVisible;
            cancelReason = source.cancelReason;
            deletionBasis = source.deletionBasis;
        }
        return *this;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    [[nodiscard]] virtual b8 setFocused(b8 focused) = 0;
};

[[nodiscard]] Expected<GlobalUniquePtr<ITextInputService>> CreateTextInputService(Alloc::GlobalArena& arena, void* nativeWindowHandle);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
