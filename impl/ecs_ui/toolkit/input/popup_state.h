// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "target_table.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class InputPopupState final : NoCopy{
public:
    explicit InputPopupState(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] b8 stage(Span<const PopupScope> scopes)noexcept;
    [[nodiscard]] Span<const PopupRecord> staged()const noexcept{ return m_stagedPopups; }
    [[nodiscard]] Span<const PopupRecord> records()const noexcept{ return m_popups; }
    [[nodiscard]] b8 canAppendDismissal()const noexcept{ return m_popupDismissals.size() != s_InputMaxPopups; }
    [[nodiscard]] b8 apply(const PopupDismissRequest& request)noexcept;
    [[nodiscard]] Expected<PopupDismissReason::Enum> consumeDismissal(const PopupToken& token)noexcept;
    [[nodiscard]] b8 dismiss(PopupDismissReason::Enum reason)noexcept;
    void close(const PopupToken& token)noexcept;
    void fence(const PopupToken& token)noexcept;
    void cancelFocus()noexcept;
    void reset()noexcept;
    [[nodiscard]] Optional<InputPopupRetirement> retire(InputTargetTable& targets, WidgetId id);
    void install(
        const Span<const HitTarget> targets,
        const Span<const TargetLookup> lookup,
        const b8 windowFocused,
        const u64 focusLossGeneration,
        const u64 expectedFocusLossGeneration,
        Vector<CommandSource>& commandSources,
        WidgetId& inOutFocus,
        u64& inOutFocusDeclaration,
        ControlToken& inOutFocusControl
    )noexcept;


private:
    Vector<PopupRecord, Core::Alloc::GlobalArena> m_popups;
    Vector<PopupRecord, Core::Alloc::GlobalArena> m_stagedPopups;
    Vector<PopupDismissal, Core::Alloc::GlobalArena> m_popupDismissals;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

