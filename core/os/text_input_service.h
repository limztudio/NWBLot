// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "text_input.h"

#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared bounded session/event lifecycle for native backends and deterministic platform fakes.
class QueuedTextInputService : public ITextInputService{
public:
    explicit QueuedTextInputService(Alloc::GlobalArena& arena);
    virtual ~QueuedTextInputService()override;


public:
    [[nodiscard]] virtual bool isOwnerThread()const noexcept override final{ return QueryCurrentThreadId() == m_ownerThread; }
    [[nodiscard]] virtual TextInputSessionToken activeSession()const noexcept override final;
    [[nodiscard]] virtual u64 surroundingRevision(TextInputSessionToken token)const noexcept override final;
    [[nodiscard]] virtual TextInputBeginResult begin(const TextInputSessionDesc& desc)override final;
    [[nodiscard]] virtual bool end(TextInputSessionToken token)override final;
    [[nodiscard]] virtual TextInputAdmission::Enum updateCaret(TextInputSessionToken token, TextInputRect caret)override final;
    [[nodiscard]] virtual TextInputAdmission::Enum updateSurrounding(
        TextInputSessionToken token, AStringView text, usize anchorByte, usize caretByte,
        TextInputChangeCause::Enum cause = TextInputChangeCause::Other
    )override final;
    [[nodiscard]] virtual TextInputPollResult::Enum poll(TextInputSessionToken token, TextInputEvent& event)override final;
    [[nodiscard]] virtual bool setFocused(bool focused)override final;


protected:
    [[nodiscard]] virtual TextInputAdmission::Enum startNativeSession(
        TextInputSessionToken token, const TextInputSessionDesc& desc
    );
    virtual void endNativeSession(TextInputSessionToken token);
    [[nodiscard]] virtual TextInputAdmission::Enum updateNativeCaret(TextInputRect caret);
    virtual void updateNativeSurrounding(
        AStringView text, usize anchorByte, usize caretByte, u64 revision, TextInputChangeCause::Enum cause
    );


protected:
    [[nodiscard]] TextInputAdmission::Enum emitCommit(TextInputSessionToken token, AStringView text);
    [[nodiscard]] TextInputAdmission::Enum emitPreedit(
        TextInputSessionToken token, AStringView text, usize anchorByte, usize caretByte, bool caretVisible = true
    );
    [[nodiscard]] TextInputAdmission::Enum emitDeleteSurrounding(
        TextInputSessionToken token, usize beforeBytes, usize afterBytes, u64 revision,
        TextInputDeletionBasis::Enum basis
    );
    [[nodiscard]] bool cancelSession(TextInputSessionToken token, TextInputCancelReason::Enum reason);
    [[nodiscard]] Alloc::GlobalArena& arena()const noexcept{ return m_arena; }
    [[nodiscard]] AStringView surroundingText()const noexcept{ return m_surrounding; }
    [[nodiscard]] usize surroundingAnchorByte()const noexcept{ return m_anchorByte; }
    [[nodiscard]] usize surroundingCaretByte()const noexcept{ return m_caretByte; }
    [[nodiscard]] u64 surroundingRevision()const noexcept{ return m_surroundingRevision; }
    [[nodiscard]] TextInputRect caretRect()const noexcept{ return m_caret; }
    [[nodiscard]] bool focused()const noexcept{ return m_focused; }


private:
    [[nodiscard]] TextInputAdmission::Enum admitEvent(
        TextInputSessionToken token, TextInputEventKind::Enum kind, AStringView text,
        usize anchorByte, usize caretByte, usize beforeBytes, usize afterBytes, bool caretVisible,
        TextInputDeletionBasis::Enum basis = TextInputDeletionBasis::Caret
    );
    void clearEvents();
    void invalidateSession();


private:
    Alloc::GlobalArena& m_arena;
    AString<Alloc::GlobalArena> m_surrounding;
    Vector<TextInputEvent, Alloc::GlobalArena> m_events;
    const ThreadId m_ownerThread;
    const u64 m_serviceIdentity;
    TextInputSessionToken m_activeToken;
    TextInputSessionToken m_cancelledToken;
    TextInputRect m_caret;
    usize m_anchorByte = 0u;
    usize m_caretByte = 0u;
    usize m_queuedTextBytes = 0u;
    u64 m_surroundingRevision = 0u;
    u64 m_nextGeneration = 1u;
    u64 m_nextSequence = 1u;
    bool m_focused = false;
    bool m_transitioning = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

