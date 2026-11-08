// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "target_table.h"

#include <impl/ecs_ui/toolkit/input/target_validation.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputTargetTable::InputTargetTable(Core::Alloc::GlobalArena& arena)
    : m_targets(arena)
    , m_stagedTargets(arena)
    , m_lookup(arena)
    , m_stagedLookup(arena)
{
    m_targets.reserve(s_InputMaxTargets);
    m_stagedTargets.reserve(s_InputMaxTargets);
    m_lookup.reserve(s_InputMaxTargets);
    m_stagedLookup.reserve(s_InputMaxTargets);
}


b8 InputTargetTable::canPublish(const usize count, const u64 generation)const noexcept{
    return count <= s_InputMaxTargets && generation > m_generation;
}

b8 InputTargetTable::publish(const Span<const HitTarget> targets, const Span<const PopupRecord> stagedPopups, const u64 generation){
    if(!canPublish(targets.size(), generation) || !StageRouterHitTargets(targets, stagedPopups, m_stagedTargets, m_stagedLookup))
        return false;
    if(!ValidateSortedStagedRouterTargetsAndPopups(m_stagedTargets, m_stagedLookup, stagedPopups))
        return false;
    m_targets.swap(m_stagedTargets);
    m_lookup.swap(m_stagedLookup);
    m_generation = generation;
    return true;
}

const HitTarget* InputTargetTable::find(const WidgetId id, const u64 declarationGeneration)const noexcept{
    const auto index = FindTargetIndexInTable(m_targets, m_lookup, id, declarationGeneration);
    return index ? &m_targets[*index] : nullptr;
}

b8 InputTargetTable::invalidate(const WidgetId id){
    if(!EraseInvalidatedRouterTargets(id, m_lookup, m_targets))
        return false;
    rebuildLookup();
    return true;
}

b8 InputTargetTable::fenceControl(const WidgetId host, const u64 declarationGeneration, const ControlToken& token)noexcept{
    return FenceRouterControlTargets(host, declarationGeneration, token, m_lookup, m_targets);
}

Optional<InputPopupRetirement> InputTargetTable::retirePopup(const WidgetId id, Vector<PopupRecord, Core::Alloc::GlobalArena>& popups, Vector<PopupDismissal, Core::Alloc::GlobalArena>& dismissals){
    InputPopupRetirement retirement;
    if(!TryRetireRouterPopupSubtree(id, popups, m_targets, dismissals, retirement.top, retirement.restore))
        return {};
    rebuildLookup();
    return retirement;
}

void InputTargetTable::reset()noexcept{
    m_targets.clear();
    m_stagedTargets.clear();
    m_lookup.clear();
    m_stagedLookup.clear();
    m_generation = 0u;
}


void InputTargetTable::rebuildLookup(){
    RebuildRouterTargetLookup(m_targets, m_lookup);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

