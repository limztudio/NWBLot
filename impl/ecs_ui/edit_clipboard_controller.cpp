// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_clipboard_controller.h"

#include <impl/ecs_ui/toolkit/edit/single_line_text.h>
#include <impl/ecs_ui/toolkit/edit/multiline_text.h>

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiEditClipboardController::UiEditClipboardController(Core::Alloc::GlobalArena& arena, Core::IClipboardService& service)
    : m_service(service)
    , m_completion(arena)
    , m_expectedModel(arena)
    , m_insertText(arena)
{}

UiEditClipboardController::~UiEditClipboardController(){
    if(!cancel())
        TerminateInvariant();
}


UiEditClipboardResult UiEditClipboardController::request(
    const UiTextEditOwner& owner, const Ui::EditModel& model, const Ui::EditClipboardAction::Enum action,
    const Core::ClipboardChannel::Enum channel, const bool readOnly){
    if(!m_service.isOwnerThread())
        return { UiEditClipboardStatus::WrongThread };
    if(!owner.valid())
        return { UiEditClipboardStatus::InvalidOwner };
    if(!cancel())
        return { UiEditClipboardStatus::NativeFailure };
    if(action == Ui::EditClipboardAction::None || action > Ui::EditClipboardAction::PublishSelection)
        return { UiEditClipboardStatus::InvalidAction };
    if(channel > Core::ClipboardChannel::PrimarySelection)
        return { UiEditClipboardStatus::Unsupported };
    const bool reads = action == Ui::EditClipboardAction::Paste;
    const bool mutates = reads || action == Ui::EditClipboardAction::Cut;
    if(readOnly && mutates)
        return { UiEditClipboardStatus::ReadOnly };
    if(model.composition().active)
        return { UiEditClipboardStatus::CompositionActive };
    if(!reads && !model.hasSelection())
        return { UiEditClipboardStatus::NoSelection };
    const auto capabilities = m_service.capabilities(channel);
    if(reads ? !capabilities.readText : !capabilities.writeText)
        return { UiEditClipboardStatus::Unsupported };
    const auto result = reads ? m_service.requestReadText(channel) : m_service.requestWriteText(channel, model.selectedText());
    if(result.admission != Core::ClipboardAdmission::Accepted)
        return { result.admission == Core::ClipboardAdmission::WrongThread
            ? UiEditClipboardStatus::WrongThread : UiEditClipboardStatus::QueueFull };
    if(!result.token.valid())
        return { UiEditClipboardStatus::NativeFailure };
    m_token = result.token;
    m_owner = owner;
    m_channel = channel;
    m_action = action;
    m_expectedModel.capture(model);
    return { UiEditClipboardStatus::Pending };
}

UiEditClipboardResult UiEditClipboardController::drain(const UiTextEditOwner& owner, Ui::EditModel& model, const bool readOnly){
    if(!m_service.isOwnerThread())
        return { UiEditClipboardStatus::WrongThread };
    if(!m_token.valid())
        return {};
    if(!(m_owner == owner) || !m_expectedModel.matches(model)){
        const auto status = m_owner == owner ? UiEditClipboardStatus::StaleModel : UiEditClipboardStatus::StaleOwner;
        return { cancel() ? status : UiEditClipboardStatus::NativeFailure };
    }
    const auto poll = m_service.poll(m_token, m_completion);
    if(poll == Core::ClipboardPollResult::Pending)
        return { UiEditClipboardStatus::Pending };
    if(poll == Core::ClipboardPollResult::WrongThread)
        return { UiEditClipboardStatus::WrongThread };
    UiEditClipboardResult result{ UiEditClipboardStatus::Cancelled };
    if(poll == Core::ClipboardPollResult::Completed){
        const auto operation = m_action == Ui::EditClipboardAction::Paste
            ? Core::ClipboardOperation::ReadText : Core::ClipboardOperation::WriteText;
        if(m_completion.token == m_token && m_completion.channel == m_channel && m_completion.operation == operation)
            result = applyCompletion(model, readOnly);
        else
            result.status = UiEditClipboardStatus::NativeFailure;
    }
    clearRequest();
    return result;
}

bool UiEditClipboardController::cancel(){
    if(!m_token.valid())
        return true;
    if(!m_service.isOwnerThread())
        return false;
    if(!m_service.cancel(m_token) && m_service.poll(m_token, m_completion) != Core::ClipboardPollResult::InvalidRequest)
        return false;
    clearRequest();
    return true;
}


void UiEditClipboardController::clearRequest(){
    m_token = {};
    m_owner = {};
    m_action = Ui::EditClipboardAction::None;
    m_expectedModel.clear();
    m_insertText.clear();
}

UiEditClipboardResult UiEditClipboardController::applyCompletion(Ui::EditModel& model, const bool readOnly){
    switch(m_completion.status){
    case Core::ClipboardStatus::Unsupported: return { UiEditClipboardStatus::Unsupported };
    case Core::ClipboardStatus::InvalidText: return { UiEditClipboardStatus::InvalidText };
    case Core::ClipboardStatus::TooLarge: return { UiEditClipboardStatus::TooLarge };
    case Core::ClipboardStatus::Success: break;
    default: return { UiEditClipboardStatus::NativeFailure };
    }
    if(m_action != Ui::EditClipboardAction::Paste && m_action != Ui::EditClipboardAction::Cut)
        return { UiEditClipboardStatus::Published };
    if(readOnly)
        return { UiEditClipboardStatus::ReadOnly };
    const u64 revision = model.revision();
    const usize anchor = model.anchor();
    const usize caret = model.caret();
    if(m_action == Ui::EditClipboardAction::Paste){
        const usize retainedBytes = model.text().size() - (model.selectionEnd() - model.selectionStart());
        if(retainedBytes > model.limits().maxBytes)
            return { UiEditClipboardStatus::TooLarge };
        const auto source = AStringView(m_completion.text);
        const usize maxBytes = model.limits().maxBytes - retainedBytes;
        const auto status = model.textMode() == Ui::EditTextMode::Multiline
            ? Ui::NormalizeMultilineText(source, m_insertText, maxBytes)
            : Ui::NormalizeSingleLineText(source, m_insertText, maxBytes)
        ;
        if(status != Ui::EditTextStatus::Accepted)
            return { status == Ui::EditTextStatus::TooLarge ? UiEditClipboardStatus::TooLarge : UiEditClipboardStatus::InvalidText };
        if(m_insertText.empty())
            return { UiEditClipboardStatus::Applied };
    }
    if(!model.replaceSelection(AStringView(m_insertText)))
        return { UiEditClipboardStatus::ModelRejected };
    return { UiEditClipboardStatus::Applied, model.revision() != revision,
        model.anchor() != anchor || model.caret() != caret };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

