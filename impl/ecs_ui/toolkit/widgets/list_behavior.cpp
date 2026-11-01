// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "list.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_list_behavior{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<u64> FindCursor(const IListDataSource& source, const u64 count, const u64 cursor, const ControlAction& action){
    if(count == 0u)
        return 0u;
    const auto current = ListBehavior::ResolveKey(source, count, cursor);
    if(!current)
        return MakeUnexpected(current.error());
    const b8 reverse = IsReverseListControlAction(action.kind);
    u64 start = reverse ? count - 1u : 0u;
    if(action.kind == ControlActionKind::Home)
        start = 0u;
    else if(action.kind == ControlActionKind::End)
        start = count - 1u;
    else if(current->present){
        const u64 distance = action.kind == ControlActionKind::PageUp || action.kind == ControlActionKind::PageDown ? action.pageRows : 1u;
        start = reverse ? current->index - Min(distance, current->index) : current->index + Min(distance, count - 1u - current->index);
    }
    const auto index = source.findEnabled(start, reverse);
    if(!index)
        return 0u;
    if(*index >= count || (reverse ? *index > start : *index < start))
        return MakeUnexpected(Failure{});
    const u64 key = source.key(*index);
    if(key == 0u)
        return MakeUnexpected(Failure{});
    const auto resolved = ListBehavior::ResolveKey(source, count, key);
    if(!resolved || !resolved->present || resolved->index != *index)
        return MakeUnexpected(Failure{});
    return key;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ListKeyResolution> ListBehavior::ResolveKey(const IListDataSource& source, const u64 count, const u64 key){
    if(key == 0u || count == 0u)
        return ListKeyResolution{};
    const auto index = source.indexOf(key);
    if(!index)
        return ListKeyResolution{};
    if(*index >= count || source.key(*index) != key)
        return MakeUnexpected(Failure{});
    return ListKeyResolution(*index, source.enabled(*index));
}

b8 ListBehavior::Reconcile(ListState& state, const IListDataSource& source){
    const u64 inputGeneration = state.m_inputGeneration;
    const u64 generation = source.instanceGeneration();
    const u64 revision = source.revision();
    if(generation == 0u || revision == 0u)
        return false;
    const b8 replacement = state.m_sourceGeneration != 0u && state.m_sourceGeneration != generation;
    u64 selected = replacement ? 0u : state.m_selected;
    u64 cursor = replacement ? 0u : state.m_cursor;
    const u64 count = source.rowCount();
    const auto selectedResolution = ResolveKey(source, count, selected);
    if(!selectedResolution)
        return false;
    b8 cursorPresent = selectedResolution->present;
    if(cursor != selected){
        const auto cursorResolution = ResolveKey(source, count, cursor);
        if(!cursorResolution)
            return false;
        cursorPresent = cursorResolution->present;
    }
    selected = selectedResolution->present ? selected : 0u;
    cursor = cursorPresent ? cursor : 0u;
    if(!SourceMatches(source, generation, revision) || state.m_inputGeneration != inputGeneration)
        return false;
    if(replacement && !state.m_scroll.setOffset(0.0))
        return false;
    const b8 changed = state.m_sourceGeneration != generation || state.m_sourceRevision != revision;
    state.m_selected = selected;
    state.m_cursor = cursor;
    state.m_ensureCursor = cursor != 0u && (state.m_ensureCursor || changed);
    state.m_sourceGeneration = generation;
    state.m_sourceRevision = revision;
    return true;
}

b8 ListBehavior::Apply(ListState& state, ListResult& result, const IListDataSource& source, const ListOptions& options, const ControlAction& action){
    using namespace __hidden_ui_list_behavior;
    const u64 inputGeneration = state.m_inputGeneration;
    const ControlToken token{ state.m_inputGeneration, state.m_sourceGeneration, state.m_sourceRevision };
    if(
        !IsValidListOptions(options) || !token.valid() || action.control != token
        || !SourceMatches(source, token.contentGeneration, token.contentRevision) || action.kind > ControlActionKind::Activate
        || ((action.kind == ControlActionKind::PageUp || action.kind == ControlActionKind::PageDown) && action.pageRows == 0u)
        || state.m_inputGeneration != inputGeneration
    )
        return false;
    if(action.kind == ControlActionKind::Wheel){
        f64 offset = 0.0;
        if(!TryComputeListWheelOffset(state.m_scroll.offset(), action.delta, action.step, action.maximum, offset) || state.m_inputGeneration != inputGeneration || !state.m_scroll.setOffset(offset))
            return false;
        state.m_ensureCursor = false;
        result.valid = true;
        return true;
    }
    const u64 count = source.rowCount();
    u64 candidate = 0u;
    const b8 activate = action.kind == ControlActionKind::Activate || action.kind == ControlActionKind::Submit;
    if(activate){
        candidate = action.kind == ControlActionKind::Activate ? action.value : state.m_cursor;
        const auto resolved = ResolveKey(source, count, candidate);
        if(!resolved)
            return false;
        if(!resolved->present){
            if(action.kind == ControlActionKind::Activate)
                return false;
            candidate = 0u;
        }
    }
    else{
        const auto cursor = FindCursor(source, count, state.m_cursor, action);
        if(!cursor)
            return false;
        candidate = *cursor;
    }
    if(!SourceMatches(source, token.contentGeneration, token.contentRevision) || state.m_inputGeneration != inputGeneration)
        return false;
    if(candidate != 0u){
        state.m_cursor = candidate;
        state.m_ensureCursor = true;
        if(activate || options.selectOnNavigate){
            result.selectionChanged = result.selectionChanged || state.m_selected != candidate;
            state.m_selected = candidate;
        }
        result.activated = result.activated || activate;
    }
    result.valid = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

