// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host.h"

#include <global/simplemath.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiEditBoxHost::applyNavigation(Ui::EditModel& model, const Event& event, NavigationBorrow& navigation,
    const Ui::EditNavigationDirection::Enum direction){
    if(m_borrowRejected || m_context.failed())
        return false;
    if(model.textMode() != Ui::EditTextMode::Multiline || model.composition().active)
        return true;
    if(
        !IsFinite(event.navigationViewportHeight) || event.navigationViewportHeight < 0.0f
        || ((direction == Ui::EditNavigationDirection::PageUp || direction == Ui::EditNavigationDirection::PageDown)
            && event.navigationViewportHeight == 0.0f)
    )
        return false;
    Ui::EditModelSnapshot expected(m_arena);
    expected.capture(model);
    const u64 modelGeneration = model.instanceGeneration();
    const Ui::EditNavigationSnapshot preferred = navigation.state.snapshot();
    const Ui::EditNavigationResult target = navigation.resolver.resolve(model, direction, preferred, event.navigationViewportHeight);
    if(
        m_borrowRejected || m_context.failed() || !expected.matches(model) || model.instanceGeneration() != modelGeneration
        || !navigation.state.matches(preferred) || !target.resolved || !IsFinite(target.preferredX)
    )
        return false;
    const auto& boundaries = model.graphemeBoundaries();
    const auto boundary = LowerBound(boundaries.begin(), boundaries.end(), target.committedByte);
    if(boundary == boundaries.end() || *boundary != target.committedByte)
        return false;
    if(!model.setSelection(event.command.extend ? expected.m_expectedAnchor : target.committedByte, target.committedByte))
        return false;
    if(!navigation.state.setPreferredX(target.preferredX))
        TerminateInvariant();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

