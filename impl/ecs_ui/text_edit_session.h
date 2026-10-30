// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_ui/components.adhd.h>
#include <impl/ecs_ui/toolkit/id.h>
#include <impl/ecs_ui/toolkit/input/popup.adhd.h>
#include <impl/ecs_ui/toolkit/edit/model_snapshot.h>

#include <core/os/text_input.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditClickKind = Ui::UiEditClickKind;
using Ui::UiTextEditOwner;

// The host validates token, sequence and owner before lending a model to a copied event.
// Surrounding deletion additionally requires the exact native publication that the event addressed.
[[nodiscard]] UiTextEditStatus::Enum ApplyUiTextEditEvent(Ui::EditModel& model, const Core::TextInputEvent& event, u64 expectedSurroundingRevision, b8 matchesPublishedModel);

// Owned OS events are applied only during a host call that lends the matching model.
// No model pointer is retained.
// Drain before host edits.
// End before a local edit/selection change, then begin again to fence queued old-selection events.
// End with the old owner/model before removal or replacement so its transient preedit is cleared as well.
class UiTextEditSession final : NoCopy{
public:
    UiTextEditSession(Core::Alloc::GlobalArena& arena, Core::ITextInputService& service)noexcept;
    ~UiTextEditSession()noexcept;


public:
    [[nodiscard]] Core::TextInputAdmission::Enum begin(const UiTextEditOwner& owner, Ui::EditModel& model, Core::TextInputRect caret);
    // Only a model matching the last applied event can refresh the native snapshot; external edits require a new session.
    [[nodiscard]] Core::TextInputAdmission::Enum refresh(const UiTextEditOwner& owner, const Ui::EditModel& model, Core::TextInputRect caret);
    [[nodiscard]] UiTextEditResult drain(const UiTextEditOwner& owner, Ui::EditModel& model);
    // Collects an owned event without lending a model. The ordered host queue validates token and sequence.
    [[nodiscard]] Core::TextInputPollResult::Enum pollOwned(Core::TextInputEvent& event);
    // Explicitly trusts a synchronous local/ordered edit; refresh remains strict for ordinary callers.
    [[nodiscard]] Core::TextInputAdmission::Enum adoptLocal(const UiTextEditOwner& owner, const Ui::EditModel& model, Core::TextInputRect caret, Core::TextInputChangeCause::Enum cause = Core::TextInputChangeCause::Other);
    [[nodiscard]] b8 end(const UiTextEditOwner& owner, Ui::EditModel& model);
    // Releases native state without a model pointer. The host clears transient preedit on its next synchronous lend.
    [[nodiscard]] b8 cancel();
    [[nodiscard]] Core::TextInputSessionToken token()const noexcept{ return m_token; }

    [[nodiscard]] const UiTextEditOwner& owner()const noexcept{ return m_owner; }

    [[nodiscard]] b8 preeditCaretVisible()const noexcept{ return m_preeditCaretVisible; }

    [[nodiscard]] u64 surroundingRevision()const noexcept{ return m_token.valid() ? m_surroundingRevision : 0u; }

    [[nodiscard]] b8 matchesPublished(const UiTextEditOwner& owner, const Ui::EditModel& model)const noexcept{
        return m_token.valid() && m_owner == owner && m_expectedModel.matches(model) && matchesPublishedModel(model);
    }


private:
    [[nodiscard]] b8 matchesPublishedModel(const Ui::EditModel& model)const noexcept;
    [[nodiscard]] Core::TextInputAdmission::Enum publishSurrounding(const Ui::EditModel& model, Core::TextInputChangeCause::Enum cause = Core::TextInputChangeCause::InputMethod);
    [[nodiscard]] b8 release();
    [[nodiscard]] UiTextEditStatus::Enum applyEvent(Ui::EditModel& model);


private:
    Core::ITextInputService& m_service;
    Core::TextInputEvent m_event;
    Ui::EditModelSnapshot m_expectedModel;
    AString<Core::Alloc::GlobalArena> m_publishedText;
    UiTextEditOwner m_owner;
    Core::TextInputSessionToken m_token;
    usize m_publishedAnchor = 0u;
    usize m_publishedCaret = 0u;
    u64 m_publishedModelRevision = 0u;
    u64 m_publishedExternalRevision = 0u;
    u64 m_publishedSelectionGeneration = 0u;
    u64 m_surroundingRevision = 0u;
    u64 m_lastSequence = 0u;
    b8 m_preeditCaretVisible = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

