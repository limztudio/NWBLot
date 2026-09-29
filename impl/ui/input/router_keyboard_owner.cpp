// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InputRouter::validKeyboardOwners()const{
    for(const HitTarget& source : m_stagedTargets){
        if(!source.keyboardOwner.valid()){
            if(source.keyboardOwnerDeclarationGeneration != 0u || !source.keyboardControl.empty())
                return false;
            continue;
        }
        if(
            !source.enabled || !source.focusable || !source.textEditable || source.owner.valid()
            || source.navigable || source.scrollable || source.keyboardOwner == source.id
            || source.keyboardOwnerDeclarationGeneration == 0u || !source.keyboardControl.valid()
        )
            return false;
        usize begin = 0u;
        usize end = m_stagedLookup.size();
        while(begin < end){
            const usize middle = begin + (end - begin) / 2u;
            if(m_stagedLookup[middle].value < source.keyboardOwner.value)
                begin = middle + 1u;
            else
                end = middle;
        }
        if(begin == m_stagedLookup.size() || m_stagedLookup[begin].value != source.keyboardOwner.value)
            return false;
        const HitTarget& host = m_stagedTargets[m_stagedLookup[begin].index];
        if(
            !host.enabled || !host.focusable || !host.navigable || host.owner.valid()
            || host.declarationGeneration != source.keyboardOwnerDeclarationGeneration
            || host.control != source.keyboardControl || host.popup != source.popup || host.layer != source.layer
        )
            return false;
    }
    return true;
}

const HitTarget* InputRouter::keyboardHost(const HitTarget& source)const{
    if(!source.textEditable || !source.keyboardOwner.valid() || !source.keyboardControl.valid() || !isInteractive(source))
        return nullptr;
    const HitTarget* host = findTarget(source.keyboardOwner, source.keyboardOwnerDeclarationGeneration);
    if(
        host == nullptr || !isInteractive(*host) || !host->focusable || !host->navigable || host->owner.valid()
        || host->control != source.keyboardControl || host->popup != source.popup || host->layer != source.layer
    )
        return nullptr;
    return host;
}

bool InputRouter::currentControlKeyOwner(const ControlKeyOwner& owner)const{
    const HitTarget* host = findTarget(owner.host, owner.declarationGeneration);
    const HitTarget* source = findTarget(owner.source, owner.sourceDeclarationGeneration);
    if(
        host == nullptr || !isInteractive(*host) || !host->navigable || host->owner.valid()
        || host->control != owner.control || host->popup != owner.popup || source == nullptr
        || !isInteractive(*source) || source->control != owner.sourceControl
    )
        return false;
    return source == host || keyboardHost(*source) == host;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

