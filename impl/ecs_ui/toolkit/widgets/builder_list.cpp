// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/list_coordinator.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ListResult Builder::virtualList(const AStringView stableKey, const IListDataSource& source, ListState& state, const ListOptions& options){
    return ListCoordinator(*this).virtualList(stableKey, source, state, options);
}

const UiSkinRegion* Builder::prepareListFrame(const IListDataSource& source, ListState& state, const ListOptions& options, const ControlToken& token, const u64 rowCount, const b8 focusOnCommit, const WidgetId commandFocus, ListFrame& outFrame){
    return ListCoordinator(*this).prepareListFrame(source, state, options, token, rowCount, focusOnCommit, commandFocus, outFrame);
}

b8 Builder::listMatches(const ListFrame& frame)const{
    return
        frame.source && listStateMatches(frame)
        && frame.source->instanceGeneration() == frame.token.contentGeneration && listStateMatches(frame)
        && frame.source->revision() == frame.token.contentRevision && listStateMatches(frame)
        && frame.source->rowCount() == frame.rowCount
        && listStateMatches(frame)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

