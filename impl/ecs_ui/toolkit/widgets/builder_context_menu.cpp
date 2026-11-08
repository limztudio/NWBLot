// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/context_menu_coordinator.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ContextMenuResult Builder::contextMenu(const AStringView stableKey, const AStringView anchorKey, const IListDataSource& source, ContextMenuState& state, const ContextMenuOptions& options){
    return ContextMenuCoordinator(*this).declareMenu(stableKey, anchorKey, source, state, options);
}

b8 Builder::contextMenuStateMatches(const ContextMenuFrame& frame)const noexcept{
    return
        popupAncestorsVisible() && frame.state && frame.state->revision() == frame.revision && frame.state->isOpen() == frame.open && frame.state->m_popup.instanceGeneration() == frame.popupToken.instanceGeneration
        && frame.state->m_popup.openGeneration() == frame.popupToken.openGeneration && frame.state->m_popup.m_parent == frame.parentToken && frame.state->m_list.inputGeneration() == frame.listToken.instanceGeneration
        && frame.state->m_owner == frame.widget.id && frame.state->m_ownerDeclaration == frame.widget.declarationGeneration && frame.state->m_anchorWidget == frame.anchor.id && frame.state->m_anchorDeclaration == frame.anchor.declarationGeneration
    ;
}

b8 Builder::contextMenuMatches(const ContextMenuFrame& frame)const{
    return
        frame.source && contextMenuStateMatches(frame)
        && frame.listToken.contentGeneration != 0u && frame.listToken.contentRevision != 0u && frame.source->instanceGeneration() == frame.listToken.contentGeneration && contextMenuStateMatches(frame)
        && frame.source->revision() == frame.listToken.contentRevision && contextMenuStateMatches(frame)
        && frame.source->rowCount() == frame.rowCount && contextMenuStateMatches(frame)
    ;
}

b8 Builder::paintContextMenus(){
    return ContextMenuCoordinator(*this).paintMenus();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

