// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 InputRouter::validCommandOwners()const noexcept{
    for(const HitTarget& source : m_stagedTargets){
        if(!HasValidHitTargetCommandOwnerProperties(source))
            return false;
        if(!source.commandOwner.valid())
            continue;
        const HitTarget* host = FindTargetInTable(m_stagedTargets, m_stagedLookup, source.commandOwner, source.commandOwnerDeclarationGeneration);
        if(!host || !host->enabled || !host->focusable || !host->navigable || host->owner.valid() || host->control != source.commandControl || host->popup != source.popup || host->layer != source.layer)
            return false;
    }
    return true;
}

const HitTarget* InputRouter::commandHost(const HitTarget& source)const noexcept{
    if(!source.textEditable || !source.commandOwner.valid() || !source.commandControl.valid() || !isInteractive(source))
        return nullptr;
    const HitTarget* host = findTarget(source.commandOwner, source.commandOwnerDeclarationGeneration);
    if(host == nullptr || !isInteractive(*host) || !host->focusable || !host->navigable || host->owner.valid() || host->control != source.commandControl || host->popup != source.popup || host->layer != source.layer)
        return nullptr;
    return host;
}

b8 InputRouter::currentControlCommandOwner(const CommandSource& held)const noexcept{
    const ControlCommandOwner& owner = held.controlOwner;
    const TargetOwner& sourceOwner = held.focusOwner;
    const HitTarget* host = findTarget(owner.host, owner.declarationGeneration);
    const HitTarget* source = findTarget(sourceOwner.target, sourceOwner.declarationGeneration);
    if(
        host == nullptr || !isInteractive(*host) || !host->navigable || host->owner.valid()
        || host->control != owner.control || host->popup != owner.popup || source == nullptr
        || !isInteractive(*source) || source->control != sourceOwner.control || source->popup != sourceOwner.popup
        || (owner.horizontal && !host->horizontalNavigation)
    )
        return false;
    return source == host || commandHost(*source) == host;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

