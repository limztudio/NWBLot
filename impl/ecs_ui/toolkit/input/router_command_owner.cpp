// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InputRouter::validCommandOwners()const noexcept{
    for(const HitTarget& source : m_stagedTargets){
        if(!source.commandOwner.valid()){
            if(source.commandOwnerDeclarationGeneration != 0u || !source.commandControl.empty())
                return false;
            continue;
        }
        if(!source.enabled || !source.focusable || !source.textEditable || source.owner.valid() || source.navigable || source.scrollable || source.commandOwner == source.id || source.commandOwnerDeclarationGeneration == 0u || !source.commandControl.valid())
            return false;
        usize begin = 0u;
        usize end = m_stagedLookup.size();
        while(begin < end){
            const usize middle = begin + (end - begin) / 2u;
            if(m_stagedLookup[middle].value < source.commandOwner.value)
                begin = middle + 1u;
            else
                end = middle;
        }
        if(begin == m_stagedLookup.size() || m_stagedLookup[begin].value != source.commandOwner.value)
            return false;
        const HitTarget& host = m_stagedTargets[m_stagedLookup[begin].index];
        if(!host.enabled || !host.focusable || !host.navigable || host.owner.valid() || host.declarationGeneration != source.commandOwnerDeclarationGeneration || host.control != source.commandControl || host.popup != source.popup || host.layer != source.layer)
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

bool InputRouter::currentControlCommandOwner(const CommandSource& held)const noexcept{
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

