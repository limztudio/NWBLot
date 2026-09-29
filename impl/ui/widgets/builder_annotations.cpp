// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../builder.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Builder::Item* Builder::annotationAnchor(const AStringView stableKey){
    const WidgetId id = MakeWidgetId(m_context.scopeId(), stableKey);
    for(auto& item : m_scope->m_items){
        if(item.state.id == id && item.state.kind != WidgetKind::Separator)
            return &item;
    }
    return nullptr;
}

bool Builder::listStateMatches(const ListFrame& frame)const{
    return popupAncestorsVisible() && frame.state && frame.state->inputGeneration() == frame.token.instanceGeneration;
}

bool Builder::listMatches(const ListFrame& frame)const{
    return
        frame.source && listStateMatches(frame)
        && frame.source->instanceGeneration() == frame.token.contentGeneration && listStateMatches(frame)
        && frame.source->revision() == frame.token.contentRevision && listStateMatches(frame)
        && frame.source->rowCount() == frame.rowCount
        && listStateMatches(frame)
    ;
}

bool Builder::paintDeferred(){
    if(!paintDeferredContents() || !validateDeferredSources() || !validateDeferredStates())
        return false;
    releaseDeferredLoans();
    return true;
}

bool Builder::paintDeferredContents(){
    return paintContextMenus() && paintCombos() && paintTooltips();
}

bool Builder::validateDeferredSources()const{
    // Every loan remains live through all later source callbacks, including those in an ended child scope.
    for(const auto& frame : m_scope->m_lists){
        if(!listMatches(frame))
            return false;
    }
    for(const auto& frame : m_scope->m_combos){
        if(!comboMatches(frame))
            return false;
    }
    for(const auto& frame : m_scope->m_contextMenus){
        if(!contextMenuMatches(frame))
            return false;
    }
    return true;
}

bool Builder::validateDeferredStates()const{
    // No source callbacks run after these model checks until every scope's loans have been released.
    for(const auto& frame : m_scope->m_lists){
        if(!listStateMatches(frame))
            return false;
    }
    for(const auto& frame : m_scope->m_combos){
        if(!comboStateMatches(frame))
            return false;
    }
    for(const auto& frame : m_scope->m_contextMenus){
        if(!contextMenuStateMatches(frame))
            return false;
    }
    for(const auto& frame : m_scope->m_tooltips){
        if(!frame.state || frame.state->revision() != frame.revision)
            return false;
    }
    return true;
}

void Builder::releaseDeferredLoans(){
    // Frozen geometry and input targets retain copied tokens and keys, never source or model loans.
    for(auto& frame : m_scope->m_lists){
        frame.source = nullptr;
        frame.state = nullptr;
    }
    for(auto& frame : m_scope->m_combos){
        frame.source = nullptr;
        frame.results = nullptr;
        frame.searchSource = nullptr;
        frame.search = nullptr;
        frame.state = nullptr;
        if(frame.editor != s_LayoutNoParent)
            m_scope->m_comboEditors[frame.editor].editState = nullptr;
    }
    for(auto& frame : m_scope->m_contextMenus){
        frame.source = nullptr;
        frame.state = nullptr;
    }
    for(auto& frame : m_scope->m_tooltips)
        frame.state = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

