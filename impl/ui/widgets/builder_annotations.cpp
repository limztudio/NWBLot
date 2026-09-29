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
    return frame.state && frame.state->inputGeneration() == frame.token.instanceGeneration;
}

bool Builder::listMatches(const ListFrame& frame)const{
    return
        frame.source && listStateMatches(frame)
        && frame.source->instanceGeneration() == frame.token.contentGeneration
        && frame.source->revision() == frame.token.contentRevision && frame.source->rowCount() == frame.rowCount
        && listStateMatches(frame)
    ;
}

bool Builder::paintDeferred(){
    if(!paintContextMenus() || !paintCombos())
        return false;
    // Every list, combo and menu remains borrowed through all callbacks in this enclosing scope.
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
    // The final model checks call no source code, so a later callback cannot invalidate an earlier model unnoticed.
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
    if(!paintTooltips())
        return false;
    releaseDeferredLoans();
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

