// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "clipboard.h"
#include "x11_checked.h"

#include <core/os/clipboard_service.h>
#include <core/os/clipboard_text.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace X11ClipboardPhase{
    enum Enum : u8{ Idle, Timestamp, Notify, Incremental };
};


class X11ClipboardService final : public QueuedClipboardService{
private:
    struct OwnedSelection{
        AString<Alloc::GlobalArena> utf8;
        AString<Alloc::GlobalArena> latin1;
        Time timestamp = 0u;
        bool latin1Available = false;
        bool owned = false;

        explicit OwnedSelection(Alloc::GlobalArena& arena)
            : utf8(arena)
            , latin1(arena)
        {}
    };

    struct OutgoingTransfer{
        AString<Alloc::GlobalArena> text;
        Window requestor = 0u;
        Atom property = 0u;
        Atom type = 0u;
        long previousEventMask = 0;
        usize offset = 0u;
        Timer deadline;

        explicit OutgoingTransfer(Alloc::GlobalArena& arena)
            : text(arena)
        {}
    };


public:
    static constexpr usize s_MaxTransfers = 8u;
    static constexpr u32 s_TransferTimeoutMs = 5000u;


public:
    X11ClipboardService(Alloc::GlobalArena& arena, Display& display);
    virtual ~X11ClipboardService()override;


public:
    [[nodiscard]] bool initialize();
    [[nodiscard]] bool handleEvent(const XEvent& event);
    [[nodiscard]] virtual ClipboardCapabilities capabilities(ClipboardChannel::Enum channel)const noexcept override;


protected:
    virtual void startNativeRequest(ClipboardRequestToken token, ClipboardOperation::Enum operation, ClipboardChannel::Enum channel, AStringView text)override;
    virtual void cancelNativeRequest(ClipboardRequestToken token)override;
    virtual void pumpNativeRequests()override;


private:
    [[nodiscard]] Atom selectionAtom(ClipboardChannel::Enum channel)const;
    [[nodiscard]] OwnedSelection* ownedSelection(Atom selection);
    void releaseOperation();
    void finishOperation(ClipboardStatus::Enum status, AStringView text = {});
    void receiveTimestamp(Time timestamp);
    void requestConversion();
    void receiveSelection(const XSelectionEvent& event);
    void receiveChunk();
    void finishRead();


private:
    void answerSelection(const XSelectionRequestEvent& event);
    [[nodiscard]] bool convertTarget(const XSelectionRequestEvent& event, Atom target, Atom property, OwnedSelection& selection);
    [[nodiscard]] bool sendText(Window requestor, Atom property, Atom type, AStringView text);
    void sendChunk(usize index);
    void releaseTransfer(usize index, bool requestorAlive);


private:
    Alloc::GlobalArena& m_arena;
    Display& m_display;
    Array<OwnedSelection, 2u> m_owned;
    Vector<OutgoingTransfer, Alloc::GlobalArena> m_outgoing;
    ClipboardTextAccumulator m_received;
    AString<Alloc::GlobalArena> m_pendingWrite;
    AString<Alloc::GlobalArena> m_decoded;
    Window m_ownerWindow = 0u;
    Window m_operationWindow = 0u;
    Atom m_clipboardAtom = 0u;
    Atom m_utf8Atom = 0u;
    Atom m_targetsAtom = 0u;
    Atom m_timestampAtom = 0u;
    Atom m_multipleAtom = 0u;
    Atom m_atomPairAtom = 0u;
    Atom m_textAtom = 0u;
    Atom m_utf8MimeAtom = 0u;
    Atom m_incrementalAtom = 0u;
    Atom m_propertyAtom = 0u;
    Atom m_timeProbeAtom = 0u;
    Atom m_operationSelection = 0u;
    Atom m_operationTarget = 0u;
    Time m_operationTimestamp = 0u;
    ClipboardRequestToken m_operationToken;
    ClipboardOperation::Enum m_operation = ClipboardOperation::ReadText;
    X11ClipboardPhase::Enum m_phase = X11ClipboardPhase::Idle;
    Timer m_deadline;
    usize m_chunkBytes = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

