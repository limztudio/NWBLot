// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/edit_box_host.h>

#include <core/os/clipboard_service.h>
#include <core/os/text_input_service.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditHostFixtureSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Impl;


class FakeTextInput final : public QueuedTextInputService{
public:
    using QueuedTextInputService::emitCommit;
    using QueuedTextInputService::caretRect;
    using QueuedTextInputService::surroundingText;


public:
    explicit FakeTextInput(Alloc::GlobalArena& arena)
        : QueuedTextInputService(arena)
    {}
    virtual ~FakeTextInput()noexcept override = default;


public:
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override{ return { true, true, true, true }; }

    [[nodiscard]] TextInputAdmission::Enum commit(const AStringView text){ return emitCommit(activeSession(), text); }

    [[nodiscard]] TextInputAdmission::Enum preedit(const AStringView text){ return emitPreedit(activeSession(), text, 0u, text.size()); }

    [[nodiscard]] TextInputAdmission::Enum preedit(const AStringView text, const usize anchor, const usize caret, const b8 visible = true){
        return emitPreedit(activeSession(), text, anchor, caret, visible);
    }

    [[nodiscard]] TextInputAdmission::Enum erase(const usize before, const usize after, const u64 revision, const TextInputDeletionBasis::Enum basis){
        return emitDeleteSurrounding(activeSession(), before, after, revision, basis);
    }


protected:
    [[nodiscard]] virtual TextInputAdmission::Enum startNativeSession(TextInputSessionToken, const TextInputSessionDesc&)noexcept override;

    virtual void endNativeSession(const TextInputSessionToken)noexcept override{ ++ends; }


public:
    u32 starts = 0u;
    u32 ends = 0u;
};

class FakeClipboard final : public QueuedClipboardService{
public:
    explicit FakeClipboard(Alloc::GlobalArena& arena, const b8 inDelayed = false)
        : QueuedClipboardService(arena)
        , document(arena)
        , startedText(arena)
        , delayed(inDelayed)
    {}
    virtual ~FakeClipboard()noexcept override = default;


public:
    [[nodiscard]] virtual ClipboardCapabilities capabilities(const ClipboardChannel::Enum channel)const noexcept override{
        return channel == ClipboardChannel::Clipboard ? ClipboardCapabilities{ true, true } : ClipboardCapabilities{};
    }

    [[nodiscard]] b8 deliver(ClipboardRequestToken token, ClipboardStatus::Enum status, AStringView text = {});

    [[nodiscard]] b8 complete(const ClipboardRequestToken token, const AStringView text){
        return completeNativeRequest(token, ClipboardStatus::Success, text);
    }


protected:
    virtual void startNativeRequest(ClipboardRequestToken token, ClipboardOperation::Enum operation, ClipboardChannel::Enum channel, AStringView text)override;


public:
    AString<Alloc::GlobalArena> document;
    AString<Alloc::GlobalArena> startedText;
    ClipboardRequestToken startedToken;
    ClipboardOperation::Enum startedOperation = ClipboardOperation::ReadText;
    b8 delayed = false;
};


[[nodiscard]] b8 PrepareEditView(const Ui::EditModel& model, Ui::TextLayoutBuilder& layoutBuilder, Ui::EditBoxView& view);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

