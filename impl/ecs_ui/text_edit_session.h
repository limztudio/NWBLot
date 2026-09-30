// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_ui/toolkit/id.h>
#include <impl/ecs_ui/toolkit/edit/model.h>

#include <core/os/text_input.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The host advances the corresponding generation when a declaration or its backing model is replaced.
struct UiTextEditOwner{
    Ui::WidgetId widget;
    u64 declarationGeneration = 0u;
    u64 modelGeneration = 0u;

    [[nodiscard]] bool valid()const{
        return widget.valid() && declarationGeneration != 0u && modelGeneration != 0u;
    }
};

[[nodiscard]] inline bool operator==(const UiTextEditOwner& lhs, const UiTextEditOwner& rhs){
    return
        lhs.widget == rhs.widget && lhs.declarationGeneration == rhs.declarationGeneration
        && lhs.modelGeneration == rhs.modelGeneration
    ;
}

namespace UiTextEditStatus{
    enum Enum : u8{ Idle, Applied, Cancelled, StaleOwner, StaleModel, StaleSurrounding, InvalidEvent, ModelRejected,
        NativeFailure, WrongThread };
};

struct UiTextEditResult{
    UiTextEditStatus::Enum status = UiTextEditStatus::Idle;
    usize eventsApplied = 0u;
    bool textChanged = false;
};

// The host validates token, sequence and owner before lending a model to a copied event.
// Surrounding deletion additionally requires the exact native publication that the event addressed.
[[nodiscard]] UiTextEditStatus::Enum ApplyUiTextEditEvent(
    Ui::EditModel& model, const Core::TextInputEvent& event, u64 expectedSurroundingRevision, bool matchesPublishedModel
);

// Owned OS events are applied only during a host call that lends the matching model. No model pointer is retained.
// Drain before host edits. End before a local edit/selection change, then begin again to fence queued old-selection events.
// End with the old owner/model before removal or replacement so its transient preedit is cleared as well.
class UiTextEditSession final : NoCopy{
public:
    UiTextEditSession(Core::Alloc::GlobalArena& arena, Core::ITextInputService& service);
    ~UiTextEditSession();


public:
    [[nodiscard]] Core::TextInputAdmission::Enum begin(
        const UiTextEditOwner& owner, Ui::EditModel& model, Core::TextInputRect caret
    );
    // Only a model matching the last applied event can refresh the native snapshot; external edits require a new session.
    [[nodiscard]] Core::TextInputAdmission::Enum refresh(
        const UiTextEditOwner& owner, const Ui::EditModel& model, Core::TextInputRect caret
    );
    [[nodiscard]] UiTextEditResult drain(const UiTextEditOwner& owner, Ui::EditModel& model);
    // Collects an owned event without lending a model. The ordered host queue validates token and sequence.
    [[nodiscard]] Core::TextInputPollResult::Enum pollOwned(Core::TextInputEvent& event);
    // Explicitly trusts a synchronous local/ordered edit; refresh remains strict for ordinary callers.
    [[nodiscard]] Core::TextInputAdmission::Enum adoptLocal(
        const UiTextEditOwner& owner, const Ui::EditModel& model, Core::TextInputRect caret,
        Core::TextInputChangeCause::Enum cause = Core::TextInputChangeCause::Other
    );
    [[nodiscard]] bool end(const UiTextEditOwner& owner, Ui::EditModel& model);
    // Releases native state without a model pointer. The host clears transient preedit on its next synchronous lend.
    [[nodiscard]] bool cancel();
    [[nodiscard]] Core::TextInputSessionToken token()const{ return m_token; }
    [[nodiscard]] const UiTextEditOwner& owner()const{ return m_owner; }
    [[nodiscard]] bool preeditCaretVisible()const{ return m_preeditCaretVisible; }
    [[nodiscard]] u64 surroundingRevision()const{ return m_token.valid() ? m_surroundingRevision : 0u; }
    [[nodiscard]] bool matchesPublished(const UiTextEditOwner& owner, const Ui::EditModel& model)const{
        return m_token.valid() && m_owner == owner && matchesModel(model) && matchesPublishedModel(model);
    }


private:
    [[nodiscard]] bool matchesModel(const Ui::EditModel& model)const;
    [[nodiscard]] bool matchesPublishedModel(const Ui::EditModel& model)const;
    void captureModel(const Ui::EditModel& model);
    [[nodiscard]] Core::TextInputAdmission::Enum publishSurrounding(
        const Ui::EditModel& model, Core::TextInputChangeCause::Enum cause = Core::TextInputChangeCause::InputMethod
    );
    [[nodiscard]] bool release();
    [[nodiscard]] UiTextEditStatus::Enum applyEvent(Ui::EditModel& model);


private:
    Core::ITextInputService& m_service;
    Core::TextInputEvent m_event;
    AString<Core::Alloc::GlobalArena> m_expectedText;
    AString<Core::Alloc::GlobalArena> m_expectedPreedit;
    AString<Core::Alloc::GlobalArena> m_publishedText;
    UiTextEditOwner m_owner;
    Core::TextInputSessionToken m_token;
    Ui::EditCompositionView m_expectedComposition;
    usize m_expectedAnchor = 0u;
    usize m_expectedCaret = 0u;
    usize m_publishedAnchor = 0u;
    usize m_publishedCaret = 0u;
    u64 m_expectedRevision = 0u;
    u64 m_expectedExternalRevision = 0u;
    u64 m_expectedSelectionGeneration = 0u;
    u64 m_expectedCompositionGeneration = 0u;
    u64 m_publishedModelRevision = 0u;
    u64 m_publishedExternalRevision = 0u;
    u64 m_publishedSelectionGeneration = 0u;
    u64 m_surroundingRevision = 0u;
    u64 m_lastSequence = 0u;
    bool m_preeditCaretVisible = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

