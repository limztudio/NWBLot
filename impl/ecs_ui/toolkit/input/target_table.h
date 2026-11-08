// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/input/input.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct InputPopupRetirement{
    PopupRecord restore;
    b8 top = false;
};


class InputTargetTable final : NoCopy{
public:
    explicit InputTargetTable(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] b8 canPublish(usize count, u64 generation)const noexcept;
    [[nodiscard]] b8 publish(Span<const HitTarget> targets, Span<const PopupRecord> stagedPopups, u64 generation);
    [[nodiscard]] const HitTarget* find(WidgetId id, u64 declarationGeneration)const noexcept;
    [[nodiscard]] b8 invalidate(WidgetId id);
    [[nodiscard]] b8 fenceControl(WidgetId host, u64 declarationGeneration, const ControlToken& token)noexcept;
    [[nodiscard]] Optional<InputPopupRetirement> retirePopup(WidgetId id, Vector<PopupRecord, Core::Alloc::GlobalArena>& popups, Vector<PopupDismissal, Core::Alloc::GlobalArena>& dismissals);
    void reset()noexcept;
    [[nodiscard]] u64 generation()const noexcept{ return m_generation; }
    [[nodiscard]] const Vector<HitTarget, Core::Alloc::GlobalArena>& targets()const noexcept{ return m_targets; }
    [[nodiscard]] Span<const TargetLookup> lookup()const noexcept{ return m_lookup; }


private:
    void rebuildLookup();


private:
    Vector<HitTarget, Core::Alloc::GlobalArena> m_targets;
    Vector<HitTarget, Core::Alloc::GlobalArena> m_stagedTargets;
    Vector<TargetLookup, Core::Alloc::GlobalArena> m_lookup;
    Vector<TargetLookup, Core::Alloc::GlobalArena> m_stagedLookup;
    u64 m_generation = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

