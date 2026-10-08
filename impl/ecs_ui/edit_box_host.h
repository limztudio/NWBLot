// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "text_edit_session.h"
#include "edit_clipboard_controller.h"
#include "clipboard_publications.h"
#include "edit_click_tracker.h"

#include <impl/ecs_ui/toolkit/context.h>
#include <impl/ecs_ui/toolkit/widgets/edit_box_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct UiEditBoxGeometry{
    Ui::PopupToken popup;
    Ui::EditBoxPlacement placement;
    Ui::PaintVector<Ui::EditBoxCaretStop> stops;
    Ui::PaintVector<Ui::EditCaretLine> lines;
    Ui::EditBoxOptions options;
    u64 generation = 0u;
    u64 revision = 0u;
    u64 externalRevision = 0u;
    u64 modelGeneration = 0u;
    Ui::EditTextMode::Enum textMode = Ui::EditTextMode::SingleLine;

    explicit UiEditBoxGeometry(Core::Alloc::GlobalArena& arena) : stops(arena), lines(arena){}
};

namespace UiEditBoxEventKind{
    enum Enum : u8{ Command, Character, Native, Selection, PastePrimary, Blur, Focus };
};

// The event-thread bridge owns snapshots and events. Application models are lent only by the current declaration.
class UiEditBoxHost final : public Ui::IEditBoxHost{
private:
    // Stack-only references for the matching loan; entries and events retain scalar identities and owned copies.
    struct NavigationBorrow{
        Ui::EditNavigationState& state;
        Ui::IEditNavigationResolver& resolver;
    };

    struct Entry{
        Ui::WidgetState widget;
        UiTextEditOwner owner;
        Ui::PopupToken popup;
        Ui::EditModelSnapshot expected;
        UiEditBoxGeometry candidate;
        UiEditBoxGeometry displayed;
        u64 seen = 0u;
        u64 focusGeneration = 0u;
        u64 retiredFocusGeneration = 0u;
        u64 navigationInstanceGeneration = 0u;
        usize dragAnchor = 0u;
        usize wordDragStart = 0u;
        usize wordDragEnd = 0u;
        bool dragging = false;
        bool wordDragging = false;
        bool preeditCaretVisible = true;
        bool actionCapable = false;
        bool focused = false;
        bool enabled = true;
        bool readOnly = false;
        bool navigationResetPending = false;
        Core::TextInputSessionToken rejectedNative;

        explicit Entry(Core::Alloc::GlobalArena& arena) : expected(arena), candidate(arena), displayed(arena){}
    };

    struct Event{
        UiTextEditOwner owner;
        Core::TextInputEvent native;
        Ui::EditCommandRequest command;
        Ui::EditNavigationDirection::Enum navigationDirection = Ui::EditNavigationDirection::Up;
        usize position = 0u;
        usize wordPosition = 0u;
        u64 geometryRevision = 0u;
        u64 focusGeneration = 0u;
        u64 geometryExternalRevision = 0u;
        u64 surroundingRevision = 0u;
        u64 surroundingModelRevision = 0u;
        u64 surroundingSelectionGeneration = 0u;
        usize surroundingAnchor = 0u;
        usize surroundingCaret = 0u;
        f32 navigationViewportHeight = 0.0f;
        UiEditBoxEventKind::Enum kind = UiEditBoxEventKind::Command;
        bool navigation = false;
        bool extend = false;
        bool dragging = false;
        bool completed = false;
        bool wordSelect = false;

        explicit Event(Core::Alloc::GlobalArena& arena) : native(arena){}
    };


public:
    UiEditBoxHost(Core::Alloc::GlobalArena& arena, Ui::Context& context,
        Core::ITextInputService& textInput, Core::IClipboardService& clipboard);
    virtual ~UiEditBoxHost()override;


public:
    [[nodiscard]] virtual Ui::EditBoxResult edit(const Ui::WidgetState& widget, Ui::EditModel& model,
        const Ui::EditBoxOptions& options)override;
    [[nodiscard]] virtual Ui::EditBoxResult editInPopup(const Ui::WidgetState& widget, Ui::EditModel& model,
        const Ui::EditBoxOptions& options, const Ui::PopupToken& popup)override;
    [[nodiscard]] virtual Ui::EditBoxResult editActions(const Ui::WidgetState& widget, Ui::EditModel& model,
        const Ui::EditBoxOptions& options, const Ui::PopupToken& popup, Ui::IEditActionSink& actions)override;
    [[nodiscard]] virtual Ui::EditBoxResult editNavigated(const Ui::WidgetState& widget, Ui::EditModel& model,
        const Ui::EditBoxOptions& options, const Ui::PopupToken& popup, Ui::EditNavigationState& navigation,
        Ui::IEditNavigationResolver& resolver, Ui::IEditActionSink& actions)override;
    [[nodiscard]] virtual bool publish(const Ui::WidgetState& widget, const Ui::EditBoxView& view,
        const Ui::EditBoxPlacement& placement, const Ui::EditBoxOptions& options)override;
    void beginFrame(u64 generation, const Ui::DisplayMetrics& display);
    void finishFrame();
    void commitFrame(u64 generation);
    void collectNative();
    void input(const Ui::InputEvent& event, Ui::WidgetId previousCapture);
    [[nodiscard]] bool character(u32 unicode);
    [[nodiscard]] bool pastePrimary(Ui::Point position);
    [[nodiscard]] bool hasTextFocus()const;
    [[nodiscard]] bool wantsTextInput()const;
    [[nodiscard]] bool takeClipboardFailure()noexcept;
    void synchronizeFocus();
    void reset();


private:
    [[nodiscard]] Entry* find(Ui::WidgetId widget)noexcept;
    [[nodiscard]] const Entry* find(Ui::WidgetId widget)const noexcept;
    [[nodiscard]] bool append(Event&& event);
    void discard(const UiTextEditOwner& owner);
    [[nodiscard]] Ui::EditBoxResult editBorrowed(const Ui::WidgetState& widget, Ui::EditModel& model,
        const Ui::EditBoxOptions& options, const Ui::PopupToken& popup, Ui::IEditActionSink* actions,
        NavigationBorrow* navigation = nullptr);
    [[nodiscard]] bool apply(Entry& entry, Ui::EditModel& model, const Ui::EditBoxOptions& options,
        Event& event, Ui::EditBoxResult& result, Ui::IEditActionSink* actions, NavigationBorrow* navigation);
    [[nodiscard]] bool applyNavigation(Ui::EditModel& model, const Event& event, NavigationBorrow& navigation,
        Ui::EditNavigationDirection::Enum direction);
    [[nodiscard]] bool applyAction(Entry& entry, Ui::EditModel& model, const Ui::EditBoxOptions& options,
        Ui::EditAction::Enum action, Ui::EditBoxResult& result, Ui::IEditActionSink& actions, NavigationBorrow* navigation);
    [[nodiscard]] bool rejectBorrowedMutation()noexcept;
    [[nodiscard]] u64 nextFocusGeneration()noexcept;
    void synchronizeSession(Entry& entry, Ui::EditModel& model, const Ui::EditBoxOptions& options, bool inputMethod);
    [[nodiscard]] Core::TextInputRect nativeCaret(const UiEditBoxGeometry& geometry)const noexcept;
    [[nodiscard]] Expected<usize> hit(const Entry& entry, Ui::Point position)const noexcept;
    [[nodiscard]] Expected<usize> hitWord(const Entry& entry, Ui::Point position)const noexcept;
    void cancelTransfers();
    void drainPublications();


private:
    Core::Alloc::GlobalArena& m_arena;
    Ui::Context& m_context;
    Core::ITextInputService& m_textInput;
    Core::IClipboardService& m_clipboardService;
    UiTextEditSession m_session;
    UiEditClipboardController m_clipboard;
    UiEditClipboardController m_primary;
    UiClipboardPublications m_publications;
    UiEditClickTracker m_clickTracker;
    Ui::PaintVector<Entry> m_entries;
    Ui::PaintVector<Event> m_events;
    Ui::EditModelSnapshot m_nativePublished;
    UiTextEditOwner m_clipboardOwner;
    UiTextEditOwner m_primaryOwner;
    Ui::DisplayMetrics m_display;
    u64 m_generation = 0u;
    u64 m_lastNativeSequence = 0u;
    u64 m_focusGeneration = 0u;
    usize m_queuedTextBytes = 0u;
    bool m_clipboardFailure = false;
    bool m_borrowed = false;
    bool m_borrowRejected = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

